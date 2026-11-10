## 시그니처
```c
KiottySpecStatus KiottySpec_File_scanGuiDraft(KiottySpec *db, const char *fileId, KiottySpecCursor **outUnknownIds, KiottySpecCursor **outMissingViews);  /* html 의 data-ks-id 를 읽어 DB 에 없는 ID(getId) 와 html 에 없는 뷰(getItem) 를 보고 */
```

## 사전조건
- fileId 는 GuiDraft 역할

## 사후조건
- *outUnknownIds 에 html 의 data-ks-id 중 DB 에 없는 ID (getId), *outMissingViews 에 html 에 없는 GUI 뷰 항목 (getItem)

## 에러 코드
- GuiDraft 가 아님: InvalidArg
- 파일을 읽을 수 없음: Io
- 없는 FILE: NotFound
- NULL 핸들·NULL 필수 인자: InvalidArg (상세는 KiottySpec_lastError)

## 소유권
- *outUnknownIds 커서는 호출자가 KiottySpec_Cursor_end
- *outMissingViews 커서는 호출자가 KiottySpec_Cursor_end
- 입력 문자열은 호출 동안만 읽는다
