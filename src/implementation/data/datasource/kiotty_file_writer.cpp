#include "implementation/data/datasource/kiotty_file_writer.h"

#include "implementation/data/datasource/kiotty_file_system.h"

namespace kiotty::spec::implementation {

namespace {

KiottySpecStatus checkOpened(int descriptor);
KiottySpecStatus writeOpenDescriptor(int descriptor, Span<const char> content);
void closeOpenDescriptor(int descriptor);

}

FileWriter::~FileWriter() { close(); }

KiottySpecStatus FileWriter::open(Span<const char> path)
{
    close();
    const DescriptorResult opened = openForWriting(path);
    _descriptor = opened ? opened.value : CLOSED_DESCRIPTOR;
    return checkOpened(_descriptor);
}

KiottySpecStatus FileWriter::write(Span<const char> content)
{ return writeOpenDescriptor(_descriptor, content); }

void FileWriter::close()
{
    closeOpenDescriptor(_descriptor);
    _descriptor = CLOSED_DESCRIPTOR;
}

namespace {

KiottySpecStatus checkOpened(int descriptor)
{ return descriptor >= 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }

KiottySpecStatus writeOpenDescriptor(int descriptor, Span<const char> content)
{
    if (descriptor < 0) { return KIOTTY_SPEC_STATUS_PRECONDITION; }
    return writeDescriptor(descriptor, content);
}

void closeOpenDescriptor(int descriptor)
{
    if (descriptor >= 0) { closeDescriptor(descriptor); }
}

}

}
