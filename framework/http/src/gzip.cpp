#include "fw/http/gzip.hpp"

#include <zlib.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace fw::http {

fw::Result<std::string> gzip_decompress(std::string_view input) {
    z_stream stream{};
    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK) {
        return std::unexpected{fw::make_error_code(fw::Errc::io_error)};
    }

    struct Guard {
        z_stream* stream;
        ~Guard() {
            inflateEnd(stream);
        }
    } guard{&stream};

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());

    std::string output;
    std::array<char, 16384> buffer{};

    while (true) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        int const status = inflate(&stream, Z_NO_FLUSH);

        output.append(buffer.data(), buffer.size() - stream.avail_out);

        if (status == Z_STREAM_END) {
            if (stream.avail_in == 0) {
                break;
            }
            if (inflateReset2(&stream, MAX_WBITS + 16) != Z_OK) {
                return std::unexpected{fw::make_error_code(fw::Errc::io_error)};
            }
            continue;
        }
        if (status == Z_OK || status == Z_BUF_ERROR) {
            if (stream.avail_in == 0) {
                break;
            }
            continue;
        }
        return std::unexpected{fw::make_error_code(fw::Errc::io_error)};
    }

    return output;
}

} // namespace fw::http
