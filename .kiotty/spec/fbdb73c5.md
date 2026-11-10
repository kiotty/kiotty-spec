## 책임
UTF-8 길이·대소문자·줄 나누기, SQL LIKE 이스케이프, 번호 ID 만들기, UTF-8 ↔ 경로 변환, 파일 읽기·쓰기, FNV-1a 해시, 현재 시각.

## 모듈 인터페이스
```cpp
namespace kiotty::core {
size_t utf8Length(std::string_view text);                       // 코드포인트 수
std::string asciiLower(std::string_view text);
bool containsInsensitive(std::string_view haystack, std::string_view needle); // ASCII 만 대소문자 무시
std::string_view trim(std::string_view text);
std::vector<std::string_view> splitLines(std::string_view text);
std::string escapeLike(std::string_view text);                  // SQL LIKE 의 %, _ 이스케이프 ('\\')
std::string numberedId(std::string_view prefix, int64_t number, int width);
std::filesystem::path pathFromUtf8(std::string_view text);
std::string pathToUtf8(const std::filesystem::path &path);
std::optional<std::string> readFile(const std::filesystem::path &path);
void writeFile(const std::filesystem::path &path, std::string_view content);
std::string fnv1a64Hex(std::string_view data);
int64_t nowEpochSeconds();
} // namespace kiotty::core
```

## 멤버 변수
- 없음 (상태 없음)

## 의존 모듈
- DSN-008 core::Error
