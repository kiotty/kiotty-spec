# 요구사항

## 목표

- 프로젝트를 Kiotty 개발 원칙에 따라 개발한다.
- Kiotty Spec 출력이 AI가 먹는 Token을 최소화하도록 변경한다.

## 개발 수칙

### 에러 처리는 반드시 Result 객체를 사용한다.

1. Result 객체가 없으면 만들어서 한다.
2. Result<Value, Code> 또는 Result<Value& ,Code>로 만들어서 사용하며, Code는 반드시 enum class여야 한다.
3. Value는 반드시 Default Constructible한다. 
4. success, failure 등 Class 내 전역 헬퍼함수가 존재한다.
5. C interface 경우, C interface는 반드시 해당 프로젝트의 모든 에러를 커버하는 에러 코드 enum이 존재한다.

### 결과는 리턴값으로 보낸다.

1. Ouput 파라미터보다 Return 값을 내보낸다.

```cpp
Output doSomething(const Input& input);

// bad
void doSomething(const Input& input, Output& output);
// bad
void doSomething(const Input& input, Output* output)
```

2. 에러 코드도 같이 리턴해야할 경우, Return 객체를 사용한다.

```cpp
Result<Output, ErrorCode> doSomething(const Input& input);

auto res = doSomething(input);
if (!res) {
    // 에러 처리
}
if (res.code != ErrorCode::SUCCESS) {
    // 에러처리
}

auto& value = res.value;
// 사용하기
```

3. 일종의 스트림(파일, 소켓, 청크 메모리, USB 장치)로부터 데이터를 읽어올 떄는 아웃풋 파라미터를 사용할 수 있다.

```cpp
int32_t readFile(FileDescriptor file, void* output, size_t length);
int32_t read(SocketDescriptor socket, void* output, size_t length);
```

4. 데이터의 크기가 커서 결과값을 Stack에 담기 힘든 경우, 멤버변수의 Reference를 return한다.

```cpp
Result<Output&, ErrorCode> getBigSomething(Container& container, const Input& input);
```

### Raw Pointer는 가능한 사용하지 않는다.

1. 멤버 변수로 비소유 Reference가 필요하면 &을 사용한 Reference로 보관하며, 생성자를 통해서 받는다.

```cpp
class Some
{
public: 
    Some(Dependency& dependency) : _dependency(dependency) {}    

private:
    Dependency& _dependency;
}
``` 

2. 클래스가 기본 생성자가 필요하여 멤버 변수로 비소유 Reference를 받을 수 없으면, 종속성을 메소드 호출에서 주입한다.

```cpp
class Some
{
public:
    Some(); //
    
    bool initialize(const AnotherDependency& dependency); // 기본 생성자를 위하여, 별도의 초기화 함수를 만듬
    void release(); // 소멸자 호출을 못 시킬 때를 대비하여, 별도의 자원 관리 함수를 만듬.
        
    void doSomething(Dependency& dependency);
    void doAnotherThing(Dependency& dependency, int32_t param1, float param2);

private:
    
}
```

3. C++ 내부에서 로직을 다룰 때, Parameter로 넘길 때도 const & 형태나 & 형태로만 사용한다.

```cpp
void doSomething1(const A& a, const B& b);
void doSomething2(A& a); // Parameter에서 A instance를 생성해서 넣을 수 없는 경우에는, const를 빼고 사용해도 좋다.

```

4. 일종의 스트림(파일, 소켓, 청크 메모리, USB 장치)로부터 데이터를 쓰거나 읽을 때는 raw pointer를 사용할 수 있다.

```cpp
void read(void* buffer, size_t length);
void write(const void* buffer, size_t length);
```

5. 자료구조 구현을 위한 core class 구현을 위해서 멤버 변수로 raw pointer를 예외적으로 허용한다.

```cpp
class Span
{
    char* data;
    size_t length;
}
```

6. C interface 구현에서는 & reference가 없기 때문에 raw pointer를 허용한다.
불변 타입일 떄는 const를 앞에 붙이며, 상태 변화가 일어나는 객체일 때는 const를 뗼 수 있다.

```c
void OrganizationProject_Module_doSomething(LibraryStruct* struct, int32_t param1, int32_t param2);
```

### 디렉토리 구조는 Clean Architecture를 기반으로 한다.

1. GUI, CLI Application, 서버 데몬을 개발할 때 아래의 디렉토리 구조에 따라 설계한다.

```bash
src/ # src 파일
    main.cpp 또는 MainActivity.kt # 앱 시작점 기준이 되는 파일
    core/ # 자료구조 등이 담김. 기능 별로 파일을 만들며 주로 Template class로 구현된 경우가 많기 때문에 Single Header로 구현.
        module1.hpp 
        module2.hpp
    di/ # 앱 전체의 종속성 관리
    i18n/ # 앱 전체에서 보이는 언어별 문자열 관리
    presentation/
        ## GUI의 경우 아래와 같이 구성한다.
            view/ # 기능에 따라 분류된 GUI
            viewModel/  
            service/ # Android와 같이 별도 Widget 구현을 위하여 사용할 수 밖에 없는 별도 모듈을 위해 마련한 공간
        ## CLI의 경우 아래와 같이 구성한다.           
            commands/ # Handler에 들어갈 Input 객체와 Output 객체 정의
            input/ # 사용자 입력을 Handler에 들어갈 Input으로 Parsing함
            handler/ # CLI에서 제공하는 API에 따른 기능 Mapping은 여기서 이루어진다. infra에서 제공하는 CLI 라이브러리를 사용한다.
                main_handler.hpp # 가독성을 위해 하부 handler의 기능을 모두 조합하는 통합 handler가 존재한다.
                function1/ 
                    function1_handler.hpp
                functino2/
                    function2_handler.hpp
            output/ # Handler의 처리 결과를 조립하여 사용자에게 보여지는 값을 만든다.
        ## 서버 데몬을 만들 경우 아래와 같이 구성한다.
           context/ # Handler와 Server Loop class들을 보관할 전역 객체
           handler/ # UDP 서버, TCP 서버 등 endpoint마다 handler 만들기
                endpoint1/
                    endpoint1_handler.hpp
                    fucntion1/
                        function1_handler.hpp
                    ... 
                endpoint2/
                    endpoint2_handler.hpp
    domain/
        entity/ # 프로젝트에 필요한 개념들을 모아두는 공간. 하드웨어적인 제약은 생각하지 않도록 설계해야 한다.
        usecases/ # 프로젝트가 제공하는 기능들을 하나의 함수 단위로 표현함. 
        # 사용자 입장에서 기능 묶음 단위로 내부 디렉토리를 구성해야 함.
            function1/
                function1_usecases.hpp
                function1_usecases.cpp
            function2/
                function1_usecases.hpp
                function2_usecases.hpp
        repository/
        # 개발자 입장에서의 기능 묶음 단위로 내부 디렉토리를 구성해야 함.
        # 예를 들어 Json 기능, 특정 Domain의 Entity들의 CRUD 담당
            module1/
                Module1Repository.hpp
            module2/
                Module2Repository.hpp
            module3/
                Module3Rpepository.hpp
        states/
        # 여러 viewModel이 구독할 앱 전체의 상태. User Event 또는 앱 내부에서 발생하는 이벤트로 인해 앱 상태가 변경되어야 하면 이것을 참조한다.
            State1.hpp
            State2.hpp
    datalayer/
        repository/ # Repository의 구현
            module1/ # 데이터 소스가 어디냐에 따라서 구현 구분..
                NativeModule1Repository.hpp # 외부 라이브러리를 이용한 기능 구현
                MockModule1Repository.hpp # 기능 테스트를 위한 Mocking Repository
                RemoteModule1Repository.hpp # 네트워크와 연결해서 기능 구현
            module2/ 
        datasource/ # 영속성 데이터를 CRUD할 수 있는 원천.
            # LocalDatabase
            # RemoteDatabase
            # LocalFile
    infra/ # OS 종속적인 기능, 외부 라이브러리나 third party에 대한 wrapper
        # json/ sqlite/ network_library/ 와 같은 thirdparty
        # sharedfile (Android SAF)/ service/ (Android service) usbmonitor (JVM 계열 언어에서만 구현 가능한 USB Detection 기능) / 와 같은 OS 종속적인 기능
        # logger/ statistics/ Log와 같이 Layer 구성과 상관없이 앱 전체를 모니터링하기 위해 필요한 기능이면 넣는다.   
```

2. 
GUI, CLI Application, 서버 데몬의 Layer의 의존도는 아래와 같이 구성된다.

```bash
core <- infra
core <- presentation
core <- domain
core <- datalayer

domain <- presentation
domain <- datalayer
datalayer <- domain/usecases

infra <- datalayer
infra <- presentation // Presentation 관련 third party일 경우에만 (Socket, CLI)

i18n <- presentation

infra <- di
domain <- di
presentation <- di
datalayer <- di
```

3. C++ 라이브러리를 개발할 떄 아래와 같은 디렉토리 구조에 따라 설계한다.

```bash
include/ # Header 파일
    library_name.h
src/
    library_name.cpp # 내부 기능과 외부 인터페이스를 잇는 얇은 어댑터
    implementation/
        context/ # 라이브러리에서 사용하는 모든 인스턴스를 보관. di 역할
        core/ # 자료구조 등 전역에서 사용하는 헬퍼 클래스
        domain/
            entity/ # 라이브러리 내부에서 사용하는 Domain 로직과 관련된 객체들
                function1/
                function2/
            usecases/ # 사용자 입장에서 보여지는 기능 분류를 기준으로 기능을 구현함
                function1/
                function2/
        data/
            infra/ # Platform 별 구현이 필요한 클래스들, Platform 구현을 감쌀 수 있을만큼만 최소한으로 구현해야 함.
                class1/
                    backend/
                    class1.hpp
                class2/
                    backend/
                    class2.hpp
                class3/
                    backend/
                    class3.hpp
            states/ # Repository의 상태를 나타내는 클래스들
                Module1/
                Module2/
                Module3/
            repository/ # 기능 단위를 들고 있는 클래스들. 
                Module1/
                Module2/
                Module3/
            datasource/ # 라이브러리에서 메모리를 공급
                # Local DB
                # Remote DB
                # 사전에 크기가 정의된 Pool들   

single_header/ # 라이브러리의 Amalgamation 폴더. 별도 스크립트를 통해서만 생성

owner
    server, event_client, stream_client
        io_loop  
            connection_host
                accepter connection
                            connection_stream / tls
                    socket
```

4. 서버와 클라이언트를 한 어플리케이션에서 동시 개발하는 경우는 아래와 같이 설계한다.

```bash
app/
    project_name # 서버를 가져다 쓰든 함.
library/
    server/ # 서버 핸들러 기능을 제공하는 라이브러리    
    client/ # 클라이언트 핸들러 기능을 제공하는 라이브러리
    protocol/ # 서버와 클라이언트 사이에서 사용되는 프로토콜 및 패킷 정의
```

### GUI, CLI 개발할 때 외부 인터페이스에 보여져야 하는 모든 문장은 아래와 같이 조립되어야 한다.

1. 보여지는 모든 문장은 Key를 부여받으며, Value에 언어별 보여져야 할 값들이 정의되있다.

```
Key(APPLICATION_NAME) - Value({English : "Test App"}, {Korean : "테스트 앱"})
Key(WARNING_MESSAGE_1) - Value({Korean : "%s 때문에 오류가 발생했습니다." }, ...)
```

2. Key,Value 집합은 i18n/ 디렉토리에서 관리한다.

3. 사용자들에게 실제로 보여져야 할 문장은 View, ViewModel에서 조립한다.

```cpp
class SomeViewModel
{
    i18n _locale;    

    // Databinding Object
    char _label[256];

    void setLabel(const char* param1) {
        const char* format_str = _locale.get(WARNING_MESSAGE_1, getCurrentLanguage());
        snprintf(_label, 256, format_str, param1);
    }    
}
```

4. 보여져야 할 데이터는 Domain 영역에 있는 모듈들을 통해 Presentation에 제공되며, 절대로 Data Layer나 Domain 영역에서 외부 인터페이스에 보여져야 하는 문장을 직접 만들지 않는다.

### 작업 순서는 반드시 명시적으로 함수의 형태로 보장한다.

1. A -> B -> C 순서대로 진행되야 하는 일은 반드시 A의 output을 B에 input으로 넣고, B의 input을 C의 output을 넣어야 되는 식으로 함수를 설계하여 수행 순서를 미리 주석이나 매뉴얼을 통해 별도로 숙지하지 않아도 호출 순서를 지키도록 유도해야 한다.  

```cpp
// 좋은 예
Class a{};
auto inputB = a.doA();
auto inputC = a.doBafterA(inputB);
auto inputD = a.doCafterB(inputC);

// 나쁜 예
Class a{};
a.doA();
a.doBafterA();
a.doCafterB();
```

2. Class의 멤버변수가 필요한 동작은 Class의 멤버 변수를 parameter로 하는 inline private function을 사용하여 동작의 input, output을 예측할 수 있게 한다.

```cpp
// 나쁜 예
bool A::doSomething(int32_t a, int32_t b) {
    _c = a + b + _d;
    if (_c > 10)
        return true;
    else
        return false;
}

// 좋은 예
// public
bool A::doSomething(int32_t a, int32_t b) {
    _c = doSomethingPrivate(a, b, _d);
        if (_c > 10)
        return true;
    else
        return false;
}
// private
int32_t A::doSomethingPrivate(int32_t a, int32_t b, const int32_t& _d) {
    return a + b + _d;
}
```

### 메소드는 Side effect를 허용하지 않는다.

1. 함수 안에서 Input과 Output으로 명시적으로 보이지 않은 상태 변화를 금지한다.

```cpp
// 나쁜 예
int32_t global_c;
int32_t doSomething(int32_t a, int32_t b)
{
    global_c = a+b + global_c;
    return global_c;
}

// 좋은 예

int32_t doSomething(int32_t a, int32_t b)
{
    return a+b; // 오직 파라미터로 들어오는 Input들만 함수의 동작에 참여함
}
``` 

2. Input으로 들어온 변수의 상태 변화는 금지하며, const를 통해 상태변화가 없음을 확실히 보여준다. 그러나, 함수의 동작이 명백히 어떤 instance의 상태변화에 목적이 있을 경우 예외로 한다.

```cpp
// 나쁜 예
int32_t doSomething(A& a, B& b)
{
    a.c = b.c;
    return a.c + b.c;
}

// 좋은 예
int32_t doSomething(const A& a, const B& b) {
    ///
}

// 예외
int32_t writeSomething(Stream& stream, const char* buffer, size_t length) {
    for (size_t i=0;i<length;i++) {
        stream.data[stream.length +i] = buffer[i];
    }
    stream.length += length;
}
```

### C++ 크로스 플랫폼 클래스는 아래와 같이 구현한다.

```cpp

// 플랫폼별 구현은 백엔드 C++ 헤더 참조
#ifdef _WIN32
#include "backend/windows_xxx.hpp"
#elif __ANDROID__
#include "backend/posix_xxx.hpp"
#else 
...
#endif 

class BackendNeededClass
{
public:
    doSomethingA();
    doSomethingB();
private:
    // Payload는 backend에서 구현.
    // 사용자 입장에서는 구현이 숨겨져있음
    PlatformPaylaod payload;
};

```

### C++ 함수는 camelCase를 쓴다.

### C++ 클래스, Enum은 CamelCase를 쓴다.

### C++ enum의 멤버는 대문자와 underbar case를 사용한다.

```cpp
enum class ErrorCode
{
    ORGANIZATION_PROJECT_SUCCESS,
    ORGANIZATION_PROJECT_INVALID_VALUE,
    ... 
};

```

### C++ 변수명은 underbar case를 쓴다.

### C++ private 변수명 앞에 _를 붙인다.

### 절대로 함수, 변수, enum member에 약어를 사용하지 않는다.

### C++ namespace 명은 상위 네임스페이스는 organization 이름, 하위 네임 스페이스는 project 명으로 한다. 임의로 결정하지 않고 사용자의 의사를 반드시 묻는다.

```cpp
namespace organization
{
    namespace projectname
    {
        namespace implementation // 외부 인터페이스에 노출되지 않는, C++ 함수, 클래스들은 이 namespace 안에 속한다.
        {
        }
    }
}
```

### 외부 인터페이스로부터 받거나 내보내는 문자열 인코딩 방식은 UTF-8로 통일한다.

1. 예외는 Windows Terminal처럼 UTF-8 의외의 문자열 인코딩을 사용할 경우, Presentation Layer에 Encoding 변환기를 두어 UTF-8로 변환한다.

### C++ 라이브러리의 Entity는 Value Object로 구현한다.

```cpp
struct Entity
{
    int32_t value1;
    int32_t value2;
};
```

### C++ 라이브러리의 Entity의 문자열 보관은 런타임 동적 할당을 제약하기 위해 최대한 고정 크기의 배열을 활용한다.

```cpp
struct Entity
{
    char name[32];
    char id[64];
    ...
}
```

### C++ 라이브러리의 Entity 크기는 Stack에도 안전하게 잡기 위해 1024 byte 이하여야 한다.

```cpp
struct Entity { ... };
static_cast(sizeof(Entity) <= 1024, "...");
```

### C++ 라이브러리의 Entity 중 대용량 데이터(이미지, 파일, 텍스트)는 Handle로 표현하며, Handle에 대한 정의는 core/ 레이어에 정의한다.

```cpp

struct Handle {
    uint16_t type; // 어떤 종류의 Handle인지. 파일? 메모리 상의 Block?
    uint16_t generation; 
    uint32_t index;
};

struct OtherEntity { //... };

struct Entity
{
    // 대용량 데이터를 보유하고 있을 경우
    Handle image;
    // 다른 Entity의 Reference를 가지고 싶은 경우
    Handle otherEntity;
    // 다른 Entity를 직접 보유할 경우
    OtherEntity otherEntity;   
}

// Type으로 구분하고 싶으면 Handle을 상속받은 세부 Handle을 재정의할 수 있음.
// Entity 영역에서 해야함.
struct ImageHandle : public Handle { ImageHandle() : _type(0x01) {} };
struct OtherEntityHandle : public Handle { OtherEntityHandle() : _type(0x02) {} };

struct Entity
{
    ImageHandle image;
    OtherEntityHandle; otherEntity;
}
```

### C++ 라이브러리의 Entity 배열은 고정 크기 vector를 core/에 구현하여 적용한다.

```cpp
template<typename Value, size_t N>
struct FixedVector {
public:
    // is_default_constructible로 제약해야 함.
    size_t capacity();
    size_t length();

    const Value& operator[](size_t index) const;
    Value& operator[](size_t index);

    bool emplace_back(Value&& value);
    bool push_back(const Value& value);
private:
    size_t length;
    Value _value[N];
}

struct OtherEntity;
struct Entity
{
    // 하위 개념의 Entity를 직접 보관할 수 있음
    FixedVector<OtherEntity, MAX_OTHER_ENTITES> childs;

    // 보관량을 늘리기 위해 Reference만 들고 있을 수도 있음
    FixedVector<OtherEntityHandle, MAX_OTHER_ENTITY_HANDLES> childs;
}

```

### C++ 라이브러리에서 필요한 할당은 어플리케이션이 시작할 때 전부 가능하도록 설계해야 한다.



### ...

```cpp
// 생성
// C interface
typedef uint64_t SomethingHandle;
struct SomethingResult
{
    ErrorCode code;
    SomethingHandle handle;
}

SomethingResult createSomething(GlobalContext* context, const struct Configuration configuration)
{
    auto& repository1 = context->repository1();
    auto& repository2 = context->repository2();

    // Usecase function 호출
    auto ret = Something::createSomething(repository1, repository2, configuration);
    // implementation과 c interface를 잇는 헬퍼 함수
    res = convertResult(ret);
    return res;    
}
// Usecase 구현
Result<ErrorCode, impl::SomethingHandle> createSomething(Repository1& repository1, Repository2& repository2, const Configuration& configuration)
{
    repository1.create(repository2, configuration);
} 


// 값 가져오기



```


### C++ 라이브러리는 Runtime에 Heap 메모리를 잡지 않는다.

```cpp
// 나쁜 예
void doSomething() {
    // 최악
    auto* buffer = new char[1024 * 1024];
    
    // Runtime에 데이터 잡음
    std::vector<char> buffer(1024);
    
}

// 좋은 예

void doSomething(int32_t param1, uint32_t param2) {
    // 작은 크기의 배열은 Stack에 직접 잡기
    char buffer[1024];
}

// 작업 단위의 맥락을 보관하는 클래스가 충분한 데이터 공간을 가지고 있을 경우
void doSomething(LocalContext& provider, int32_t param1, uint32_t param2) {
    // 미리 할당된 클래스 내부의 데이터 사용
    char* data = provider.data();
}

// 작업 단위의 맥락을 보관하는 클래스와 데이터 할당을 담당하는 클래스가 분리되있을 경우
void doSomething(DataProvider& provider, LocalContext& context, int32_t param1, uint32_t param2) {
    // Local Context를 위해 할당된 데이터를 받아와서 사용
    char* data = provider.getData(context);
    if (!data) {
        // Data Provider에서 허용가능한 Local Context를 넘으면 오류 뱉기
    }
}

// 추천하는 패턴
struct RAIIHelper
{
public:
    RAIIHelper(DataProvider& provider, LocalContext& context) {
        _data = provider.getData(context);
    }
    ~RAIIHelper() {
        provider.releaseData(context);
    }

    // Data Acqusition 실패
    operator bool() { return _data; }

    // 복사 생성자 허용 X
    // 이동 생성자는 허용
    char* data();

private:
    char* _data;
}

void doSomething(DataProvider& provider, LocalContext& context, int32_t param1, uint32_t param2) {
    RAIIHelper helper {provider, context};
    if (helper) {
        auto* data = helper.data();
        // ...
    }
}

```

