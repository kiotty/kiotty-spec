#include "kiotty_commands.h"

namespace kiotty::cli {

const std::vector<CommandEntry>& commands()
{
    static const std::vector<CommandEntry> table = {
        {"init", "", "[--name N]", "프로젝트 이름과 만든 시각을 쓴다", initProject},
        {"item", "add", "KIND --title T [--id ID] [--summary S] [--parent ID] [--body T|--body-file F] [--tag 이름=값]...",
         "항목을 만들고 ID 를 찍는다. --id 가 없으면 발급한다. 이미 있는 ID 는 거부한다", addItem},
        {"item", "rename", "ID 새ID", "ID 만 바꾼다 (관계 · 이력은 그대로 따라온다)", renameItem},
        {"item", "set", "ID [--title T] [--summary S] [--parent ID|-] [--body T|--body-file F] [--tag 이름=값]...", "항목을 고친다", setItem},
        {"item", "show", "ID [--tag 이름]... [--tags] [--relation 이름]... [--relations]", "항목 한 줄", showItem},
        {"item", "list", "[--kind K] [--tag 이름[=값,...]]... [--tags] [--relation 이름]... [--relations]", "항목 목록 (8.3, 8.4)", listItems},
        {"item", "children", "ID", "하위 항목 목록", listChildren},
        {"item", "body", "ID", "본문 (본문을 찍는 유일한 명령, 3.7)", showBody},
        {"item", "remove", "ID", "항목을 지운다", removeItem},
        {"tag", "show", "ID", "항목의 태그 값", showTags},
        {"tag", "set", "ID 태그이름 값[,값]|-", "태그 값을 바꾼다. - 는 지움", setTag},
        {"relation", "add", "FROM 관계이름 TO", "관계를 더한다", addRelation},
        {"relation", "remove", "FROM 관계이름 TO", "관계를 지운다", removeRelation},
        {"relation", "list", "ID [--up|--down]", "항목의 관계 (깊이 1)", listRelations},
        {"impact", "", "ID [--depth N]", "이 항목을 가리키는 하류 항목 ID", showImpact},
        {"checkpoint", "start", "STEP [--description D]", "체크포인트를 시작한다 (STARTED)", startCheckpoint},
        {"checkpoint", "resume", "", "가장 최근 체크포인트를 RUNNING 으로", resumeCheckpoint},
        {"checkpoint", "complete", "--actor user", "확정 조건을 검사하고 DONE 으로 (사용자만)", completeCheckpoint},
        {"checkpoint", "show", "[CP]", "체크포인트 한 줄 (기본: 가장 최근)", showCheckpoint},
        {"checkpoint", "list", "[--step N]", "체크포인트 목록", listCheckpoints},
        {"checkpoint", "prune", "CP --actor user", "CP 이전 기록을 지운다 (사용자만)", pruneCheckpoints},
        {"where", "", "", "현재 단계", showPosition},
        {"history", "", "[--depth N|all]", "현재 체크포인트부터 조상 쪽으로 N 개 (기본 3)", showHistory},
        {"decision", "show", "KIND", "결정 기록", showDecision},
        {"decision", "set", "KIND (--text T | --file F)", "결정 기록을 다시 쓴다 (2048 바이트 이하)", setDecision},
        {"diff", "", "FROM TO [--kind K (--added|--removed|--modified)... [--title] [--summary] [--tag 이름]... [--tags] [--relation 이름]... [--relations] [--no-color]]",
         "체크포인트 사이 비교. --kind 가 없으면 Kind 별 요약 (5.4)", runDiff},
        {"model", "", "[KIND]", "Kind 목록, 또는 Kind 의 태그 · 관계 정의", showModel},
    };
    return table;
}


}
