#include "implementation/data/datasource/kiotty_file_writer.h"

#include "implementation/data/datasource/kiotty_file_system.h"

namespace kiotty::spec::implementation {

FileWriter::~FileWriter() { close(); }

KiottySpecStatus FileWriter::open(Span<const char> path)
{
    close();
    _descriptor = openForWriting(path);
    return _descriptor >= 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

KiottySpecStatus FileWriter::write(Span<const char> content)
{
    if (_descriptor < 0) { return KIOTTY_SPEC_STATUS_PRECONDITION; }
    return writeDescriptor(_descriptor, content);
}

void FileWriter::close()
{
    if (_descriptor >= 0) { closeDescriptor(_descriptor); }
    _descriptor = -1;
}

}
