#pragma once

extern "C" {

const char* KiottyViewer_open(const char* path);
const char* KiottyViewer_query(const char* request);
void KiottyViewer_close();

}
