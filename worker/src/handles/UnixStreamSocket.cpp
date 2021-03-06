/**
 * NOTE: This code cannot log to the Channel since this is the base code of the
 * Channel.
 */

#define MS_CLASS "UnixStreamSocket"
// #define MS_LOG_DEV

#include "handles/UnixStreamSocket.h"
#include "DepLibUV.h"
#include "MediaSoupError.h"
#include "Logger.h"
#include <cstring> // std::memcpy()
#include <cstdlib> // std::malloc(), std::free()

/* Static methods for UV callbacks. */

static inline
void on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	static_cast<UnixStreamSocket*>(handle->data)->onUvReadAlloc(suggested_size, buf);
}

static inline
void on_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf)
{
	static_cast<UnixStreamSocket*>(handle->data)->onUvRead(nread, buf);
}

static inline
void on_write(uv_write_t* req, int status)
{
	UnixStreamSocket::UvWriteData* write_data = static_cast<UnixStreamSocket::UvWriteData*>(req->data);
	UnixStreamSocket* socket = write_data->socket;

	// Delete the UvWriteData struct (which includes the uv_req_t and the store char[]).
	std::free(write_data);

	// Just notify the UnixStreamSocket when error.
	if (status)
		socket->onUvWriteError(status);
}

static inline
void on_shutdown(uv_shutdown_t* req, int status)
{
	static_cast<UnixStreamSocket*>(req->data)->onUvShutdown(req, status);
}

static inline
void on_close(uv_handle_t* handle)
{
	static_cast<UnixStreamSocket*>(handle->data)->onUvClosed();
}

static inline
void on_error_close(uv_handle_t* handle)
{
	delete handle;
}

/* Instance methods. */

UnixStreamSocket::UnixStreamSocket(int fd, size_t bufferSize) :
	bufferSize(bufferSize)
{
	MS_TRACE_STD();

	int err;

	this->uvHandle = new uv_pipe_t;
	this->uvHandle->data = (void*)this;

	err = uv_pipe_init(DepLibUV::GetLoop(), this->uvHandle, 0);
	if (err)
	{
		delete this->uvHandle;
		this->uvHandle = nullptr;
		MS_THROW_ERROR_STD("uv_pipe_init() failed: %s", uv_strerror(err));
	}

	err = uv_pipe_open(this->uvHandle, fd);
	if (err)
	{
		uv_close((uv_handle_t*)this->uvHandle, (uv_close_cb)on_error_close);
		MS_THROW_ERROR_STD("uv_pipe_open() failed: %s", uv_strerror(err));
	}

	// Start reading.
	err = uv_read_start((uv_stream_t*)this->uvHandle, (uv_alloc_cb)on_alloc, (uv_read_cb)on_read);
	if (err)
	{
		uv_close((uv_handle_t*)this->uvHandle, (uv_close_cb)on_error_close);
		MS_THROW_ERROR_STD("uv_read_start() failed: %s", uv_strerror(err));
	}

	// NOTE: Don't allocate the buffer here. Instead wait for the first uv_alloc_cb().
}

UnixStreamSocket::~UnixStreamSocket()
{
	MS_TRACE_STD();

	if (this->uvHandle)
		delete this->uvHandle;
	if (this->buffer)
		delete[] this->buffer;
}

void UnixStreamSocket::Close()
{
	MS_TRACE_STD();

	if (this->isClosing)
		return;

	int err;

	this->isClosing = true;

	// Don't read more.
	err = uv_read_stop((uv_stream_t*)this->uvHandle);
	if (err)
		MS_ABORT("uv_read_stop() failed: %s", uv_strerror(err));

	// If there is no error and the peer didn't close its pipe side then close gracefully.
	if (!this->hasError && !this->isClosedByPeer)
	{
		// Use uv_shutdown() so pending data to be written will be sent to the peer before closing.
		uv_shutdown_t* req = new uv_shutdown_t;
		req->data = (void*)this;
		err = uv_shutdown(req, (uv_stream_t*)this->uvHandle, (uv_shutdown_cb)on_shutdown);
		if (err)
			MS_ABORT("uv_shutdown() failed: %s", uv_strerror(err));
	}
	// Otherwise directly close the socket.
	else {
		uv_close((uv_handle_t*)this->uvHandle, (uv_close_cb)on_close);
	}
}

void UnixStreamSocket::Write(const uint8_t* data, size_t len)
{
	if (this->isClosing)
		return;

	if (len == 0)
		return;

	uv_buf_t buffer;
	int written;
	int err;

	// First try uv_try_write(). In case it can not directly send all the given data
	// then build a uv_req_t and use uv_write().

	buffer = uv_buf_init((char*)data, len);
	written = uv_try_write((uv_stream_t*)this->uvHandle, &buffer, 1);

	// All the data was written. Done.
	if (written == (int)len)
	{
		return;
	}
	// Cannot write any data at first time. Use uv_write().
	else if (written == UV_EAGAIN || written == UV_ENOSYS)
	{
		// Set written to 0 so pending_len can be properly calculated.
		written = 0;
	}
	// Error. Should not happen.
	else if (written < 0)
	{
		MS_ERROR_STD("uv_try_write() failed, closing the socket: %s", uv_strerror(written));
		Close();
		return;
	}

	size_t pending_len = len - written;

	// Allocate a special UvWriteData struct pointer.
	UvWriteData* write_data = (UvWriteData*)std::malloc(sizeof(UvWriteData) + pending_len);

	write_data->socket = this;
	std::memcpy(write_data->store, data + written, pending_len);
	write_data->req.data = (void*)write_data;

	buffer = uv_buf_init((char*)write_data->store, pending_len);

	err = uv_write(&write_data->req, (uv_stream_t*)this->uvHandle, &buffer, 1, (uv_write_cb)on_write);
	if (err)
		MS_ABORT("uv_write() failed: %s", uv_strerror(err));
}

inline
void UnixStreamSocket::onUvReadAlloc(size_t suggested_size, uv_buf_t* buf)
{
	MS_TRACE_STD();

	// If this is the first call to onUvReadAlloc() then allocate the receiving buffer now.
	if (!this->buffer)
		this->buffer = new uint8_t[this->bufferSize];

	// Tell UV to write after the last data byte in the buffer.
	buf->base = (char *)(this->buffer + this->bufferDataLen);
	// Give UV all the remaining space in the buffer.
	if (this->bufferSize > this->bufferDataLen)
	{
		buf->len = this->bufferSize - this->bufferDataLen;
	}
	else
	{
		buf->len = 0;

		MS_ERROR_STD("no available space in the buffer");
	}
}

inline
void UnixStreamSocket::onUvRead(ssize_t nread, const uv_buf_t* buf)
{
	MS_TRACE_STD();

	if (nread == 0)
		return;

	// Data received.
	if (nread > 0)
	{
		// Update the buffer data length.
		this->bufferDataLen += (size_t)nread;

		// Notify the subclass.
		userOnUnixStreamRead();
	}
	// Peer disconneted.
	else if (nread == UV_EOF || nread == UV_ECONNRESET)
	{
		this->isClosedByPeer = true;

		// Close local side of the pipe.
		Close();
	}
	// Some error.
	else
	{
		MS_ERROR_STD("read error, closing the pipe: %s", uv_strerror(nread));

		this->hasError = true;

		// Close the socket.
		Close();
	}
}

inline
void UnixStreamSocket::onUvWriteError(int error)
{
	MS_TRACE_STD();

	if (this->isClosing)
		return;

	if (error == UV_EPIPE || error == UV_ENOTCONN)
	{
		MS_ERROR_STD("write error, closing the pipe: %s", uv_strerror(error));
	}
	else
	{
		MS_ERROR_STD("write error, closing the pipe: %s", uv_strerror(error));

		this->hasError = true;
	}

	Close();
}

inline
void UnixStreamSocket::onUvShutdown(uv_shutdown_t* req, int status)
{
	MS_TRACE_STD();

	delete req;

	if (status == UV_EPIPE || status == UV_ENOTCONN || status == UV_ECANCELED)
		MS_ERROR_STD("shutdown error: %s", uv_strerror(status));
	else if (status)
		MS_ERROR_STD("shutdown error: %s", uv_strerror(status));

	// Now do close the handle.
	uv_close((uv_handle_t*)this->uvHandle, (uv_close_cb)on_close);
}

inline
void UnixStreamSocket::onUvClosed()
{
	MS_TRACE_STD();

	// Notify the subclass.
	userOnUnixStreamSocketClosed(this->isClosedByPeer);

	// And delete this.
	delete this;
}
