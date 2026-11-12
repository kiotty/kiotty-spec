#include "implementation/data/datasource/kiotty_file_writer.h"

namespace kiotty::spec::implementation {

KiottySpecStatus FileWriter::write(Span<const char> content)
{
    const bool is_open = _file.isOpen();
    if (!is_open) { return KIOTTY_SPEC_STATUS_PRECONDITION; }
    return writeDescriptor(_file.descriptor(), content);
}

}
