#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : 
    _capacity(capacity),
    _buffer(),
    _bytes_written(0),
    _bytes_read(0),
    _error(false),  //!< Flag indicating that the stream suffered an error.
    _close(false)
    {}

size_t ByteStream::write(const string &data) {
    if(_close || remaining_capacity() == 0)
        return 0;
    size_t bytes_to_write = min(remaining_capacity(), data.size());
    _bytes_written += bytes_to_write;
    for(size_t i = 0; i < bytes_to_write; i++)
        _buffer.push_back(data[i]);
    return bytes_to_write;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t pop_len = min(len, _buffer.size());
    return string(_buffer.begin(),_buffer.begin() + pop_len );
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) { 
    size_t pop_size = min(len, _buffer.size());
    _bytes_read += pop_size;
    for (size_t i = 0; i < pop_size; i++)
        _buffer.pop_front();
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    std::string data = peek_output(len);
    pop_output(data.size());  // pop the actual returned size
    return data;
}

void ByteStream::end_input() { _close = true;}

bool ByteStream::input_ended() const { return _close == true; }

size_t ByteStream::buffer_size() const { return _buffer.size(); }

bool ByteStream::buffer_empty() const { return _buffer.empty(); }

bool ByteStream::eof() const { return _close && _buffer.size() == 0; }

size_t ByteStream::bytes_written() const { return _bytes_written; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - _buffer.size(); }
