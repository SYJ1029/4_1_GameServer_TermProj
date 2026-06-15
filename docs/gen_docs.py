# gen_docs.py  —  게임설명서.docx + 발표자료.pptx 생성
import os, sys
OUT = os.path.dirname(os.path.abspath(__file__))

# ──────────────────────────────────────────────
#  DOCX  게임설명서
# ──────────────────────────────────────────────
from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml.ns import qn

def set_font(run, size=11, bold=False, color=None, mono=False):
    run.font.size = Pt(size)
    run.font.bold = bold
    if mono:
        run.font.name = "Courier New"
        run._element.rPr.get_or_add_rFonts().set(qn("w:eastAsia"), "Courier New")
    else:
        run.font.name = "맑은 고딕"
        run._element.rPr.get_or_add_rFonts().set(qn("w:eastAsia"), "맑은 고딕")
    if color:
        run.font.color.rgb = RGBColor(*color)

def add_h1(doc, text):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(12)
    p.paragraph_format.space_after  = Pt(4)
    p.paragraph_format.keep_with_next = True
    run = p.add_run(text)
    set_font(run, size=13, bold=True, color=(0x1B, 0x2A, 0x4A))
    return p

def add_h2(doc, text):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(8)
    p.paragraph_format.space_after  = Pt(2)
    p.paragraph_format.keep_with_next = True
    run = p.add_run(text)
    set_font(run, size=11, bold=True, color=(0x1B, 0x2A, 0x4A))
    return p

def add_body(doc, text, indent=0):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(0)
    p.paragraph_format.space_after  = Pt(3)
    if indent:
        p.paragraph_format.left_indent = Cm(indent * 0.5)
    run = p.add_run(text)
    set_font(run, size=11)
    return p

def add_code(doc, text):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(2)
    p.paragraph_format.space_after  = Pt(2)
    p.paragraph_format.left_indent  = Cm(0.7)
    p.style = doc.styles["No Spacing"]
    for line in text.strip().splitlines():
        if p.runs:
            p.add_run("\n")
        run = p.add_run(line)
        set_font(run, size=9, mono=True, color=(0x20, 0x20, 0x20))
    return p

def add_table(doc, headers, rows, col_widths=None):
    table = doc.add_table(rows=1+len(rows), cols=len(headers))
    table.style = "Table Grid"
    # header
    hrow = table.rows[0]
    for i, h in enumerate(headers):
        cell = hrow.cells[i]
        cell.paragraphs[0].clear()
        run = cell.paragraphs[0].add_run(h)
        set_font(run, size=10, bold=True)
        cell.paragraphs[0].paragraph_format.space_before = Pt(1)
        cell.paragraphs[0].paragraph_format.space_after  = Pt(1)
        from docx.oxml.ns import qn as _qn
        from lxml import etree as _et
        tc = cell._tc
        tcPr = tc.get_or_add_tcPr()
        shd = _et.SubElement(tcPr, _qn("w:shd"))
        shd.set(_qn("w:fill"),  "D9E2F3")
        shd.set(_qn("w:color"), "auto")
        shd.set(_qn("w:val"),   "clear")
    # data
    for ri, row_data in enumerate(rows):
        drow = table.rows[ri+1]
        for ci, val in enumerate(row_data):
            cell = drow.cells[ci]
            cell.paragraphs[0].clear()
            run = cell.paragraphs[0].add_run(str(val))
            set_font(run, size=10)
            cell.paragraphs[0].paragraph_format.space_before = Pt(1)
            cell.paragraphs[0].paragraph_format.space_after  = Pt(1)
    # col widths
    if col_widths:
        for ci, w in enumerate(col_widths):
            for row in table.rows:
                row.cells[ci].width = Cm(w)
    doc.add_paragraph()

def build_docx():
    doc = Document()
    # 기본 여백
    for sec in doc.sections:
        sec.top_margin    = Cm(2.5)
        sec.bottom_margin = Cm(2.5)
        sec.left_margin   = Cm(3.0)
        sec.right_margin  = Cm(2.5)

    # 제목
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run("SIMPLEST MMORPG 게임 설명서")
    set_font(run, size=16, bold=True, color=(0x1B, 0x2A, 0x4A))
    p2 = doc.add_paragraph()
    p2.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r2 = p2.add_run("학번: 2021184018  |  과목: 게임서버프로그래밍 텀프로젝트")
    set_font(r2, size=11, color=(0x55, 0x55, 0x55))
    doc.add_paragraph()

    # ── 1. 게임 개요 ──────────────────────────────────────
    add_h1(doc, "1. 게임 개요")
    add_table(doc,
        ["항목", "내용"],
        [
            ["장르",       "타일 기반 멀티플레이어 RPG (MMORPG)"],
            ["맵 크기",    "2000 × 2000 (타일 단위)"],
            ["시야 범위",  "플레이어 기준 ±5칸 (11×11 격자)"],
            ["최대 접속",  "10,000명 (성능 테스트 5,000명 달성)"],
            ["NPC 수",     "200,000마리 (Peace 100k / Agro 100k / Boss 15)"],
            ["서버",       "Windows IOCP (C++), Winsock2"],
            ["클라이언트", "SFML 2.5.1 (C++), TCP 3500번 포트"],
            ["DB",         "MS SQL Server (ODBC DSN: SYJ_TERM_2021184018)"],
        ],
        col_widths=[4.0, 11.0]
    )

    # ── 2. 프로토콜 ────────────────────────────────────────
    add_h1(doc, "2. 프로토콜")
    add_h2(doc, "2.1 공통 구조")
    add_body(doc, "모든 패킷은 #pragma pack(1) 적용. 첫 1바이트가 패킷 전체 크기.")
    add_code(doc, "[1B size] [1B type(enum)] [payload ...]      포트: TCP 3500")

    add_h2(doc, "2.2 C2S 패킷 (클라이언트 → 서버)")
    add_table(doc,
        ["타입", "구조체", "주요 필드", "설명"],
        [
            ["C2S_LOGIN",         "C2S_Login",        "username[20]",   "로그인 요청"],
            ["C2S_MOVE",          "C2S_Move",         "dir, move_time", "이동 (UP/DOWN/LEFT/RIGHT)"],
            ["C2S_ATTACK",        "C2S_Attack",       "-",              "A키 근접 공격 (바라보는 방향 앞 1칸)"],
            ["C2S_RANGED_ATTACK", "C2S_RangedAttack", "-",              "D키 직선 원거리 공격 (6타일)"],
            ["C2S_SKILL",         "C2S_Skill",        "-",              "S키 3×3 광역 스킬"],
            ["C2S_CHAT",          "C2S_Chat",         "msg[128]",       "채팅 메시지"],
            ["C2S_USE_ITEM",      "C2S_UseItem",      "item_type",      "아이템 사용 (슬롯 1~6)"],
        ],
        col_widths=[3.5, 3.5, 3.5, 5.0]
    )

    add_h2(doc, "2.3 S2C 패킷 (서버 → 클라이언트)")
    add_table(doc,
        ["타입", "주요 필드", "설명"],
        [
            ["S2C_LOGIN_RESULT", "success, message[50]",                             "로그인 성공/실패"],
            ["S2C_AVATAR_INFO",  "playerId, x, y, hp, max_hp, level, exp, exp_next", "내 캐릭터 초기화"],
            ["S2C_ADD_PLAYER",   "playerId, username, x, y, hp, max_hp, npc_type, npc_state", "시야 내 오브젝트 등장"],
            ["S2C_REMOVE_PLAYER","playerId",                                          "시야 이탈"],
            ["S2C_MOVE_PLAYER",  "playerId, x, y, move_time",                        "이동 브로드캐스트"],
            ["S2C_STAT_INFO",    "object_id, hp, max_hp, level, exp, exp_next",      "HP/레벨/EXP 갱신"],
            ["S2C_DAMAGE_INFO",  "attacker_id, target_id, damage, target_hp",        "전투 피해"],
            ["S2C_PROJECTILE",   "attacker_id, x, y, dir, range",                    "원거리 투사체 브로드캐스트"],
            ["S2C_CHAT",         "sender_id, sender_name, msg",                      "채팅 수신"],
            ["S2C_ITEM_APPEAR",  "item_id, x, y, item_type",                         "월드 아이템 생성"],
            ["S2C_ITEM_REMOVE",  "item_id",                                           "월드 아이템 제거"],
            ["S2C_ITEM_ADD",     "item_type, count",                                  "인벤토리 수량 갱신"],
            ["S2C_QUEST_UPDATE", "quest_id, state, current, target",                  "퀘스트 진행 상태"],
        ],
        col_widths=[3.8, 6.2, 5.5]
    )

    add_h2(doc, "2.4 열거형")
    add_code(doc,
        "DIRECTION  : UP=0, DOWN=1, LEFT=2, RIGHT=3\n"
        "NPC_KIND   : NPC_PC=0, NPC_PEACE=1, NPC_AGRO=2, NPC_BOSS=3\n"
        "NPC_STATE  : IDLE=0, ROAMING=1, CHASE=2\n"
        "QUEST_STATE: Q_NONE=0, Q_ACTIVE=1, Q_COMPLETED=2\n"
        "ITEM_TYPE  : NONE=0, HP_POTION=1, HI_POTION=2, ELIXIR=3,\n"
        "             ATK_BOOST=4, DEF_BOOST=5, SPD_BOOST=6"
    )

    # ── 3. 자료구조 ────────────────────────────────────────
    add_h1(doc, "3. 자료구조")
    add_h2(doc, "3.1 오브젝트 계층")
    add_code(doc,
        "CObject   (id, x, y, hp, max_hp, level, state)\n"
        "├─ SESSION (소켓, recv버퍼, xp, inventory[6], quests[2], 버프 만료시각)\n"
        "└─ CNPC    (npc_type, target_id, origin_x/y, move_state)"
    )
    add_body(doc, "전역 저장: tbb::concurrent_unordered_map<int, atomic<shared_ptr<CObject>>> clients")
    add_body(doc, "  · ID 0 ~ 9,999 : 플레이어    · ID 10,000 ~ 209,999 : NPC")

    add_h2(doc, "3.2 SESSION 주요 필드")
    add_table(doc,
        ["필드", "타입", "설명"],
        [
            ["m_client",          "SOCKET",       "클라이언트 소켓"],
            ["m_xp",              "int",          "현재 경험치"],
            ["m_inventory[6]",    "int[]",        "슬롯별 보유 수량"],
            ["m_quests[2]",       "QuestProgress[]", "퀘스트별 킬카운트"],
            ["m_atk_boost_until", "time_point",   "공격력 버프 만료 시각"],
            ["m_def_boost_until", "time_point",   "방어력 버프 만료 시각"],
            ["m_spd_boost_until", "time_point",   "이동속도 버프 만료 시각"],
            ["m_visible_objects", "unordered_set<int>", "현재 시야 내 오브젝트 ID"],
        ],
        col_widths=[4.5, 3.5, 7.5]
    )

    add_h2(doc, "3.3 CNPC 주요 필드")
    add_table(doc,
        ["필드", "타입", "설명"],
        [
            ["m_npc_type",   "int",      "Peace(1) / Agro(2) / Boss(3) 구분"],
            ["m_target_id",  "int",      "추격 대상 플레이어 ID (-1=없음)"],
            ["m_origin_x/y", "short",    "스폰 원점 (로밍 & 리스폰 기준)"],
            ["m_move_state", "NPC_STATE","IDLE / ROAMING / CHASE"],
        ],
        col_widths=[4.5, 3.5, 7.5]
    )

    add_h2(doc, "3.4 섹터 시스템")
    add_body(doc, "섹터 크기 = VIEW_RANGE × 2 + 1 = 11 × 11 타일")
    add_body(doc, "총 섹터 수 = ceil(2000/11) × ceil(2000/11) = 182 × 182 = 33,124개")
    add_body(doc, "각 섹터는 unordered_set<int>로 오브젝트 ID 관리.")
    add_body(doc, "플레이어 이동 시 인접 3×3 섹터(최대 9개)에서 시야 계산 → ADD/REMOVE 브로드캐스트.")

    add_h2(doc, "3.5 DB 스키마")
    add_body(doc, "Players 테이블")
    add_code(doc,
        "login_id  NVARCHAR(20) PK\n"
        "x, y      SMALLINT          -- 마지막 위치\n"
        "hp        SMALLINT          -- 마지막 HP\n"
        "level     INT\n"
        "exp       INT\n"
        "inv1~inv6 INT               -- 인벤토리 슬롯별 수량"
    )
    add_body(doc, "player_quests 테이블")
    add_code(doc,
        "login_id   NVARCHAR(20) FK\n"
        "quest_id   INT               -- 0=Agro Slayer, 1=Boss Hunter\n"
        "kill_count INT\n"
        "PRIMARY KEY (login_id, quest_id)"
    )
    add_table(doc,
        ["저장 프로시저", "역할"],
        [
            ["sp_LoginOrCreate",   "로그인: x,y,hp,level,exp,inv1~6 조회 (미등록 시 실패)"],
            ["sp_SavePlayer",      "x,y,hp,level,exp,inv1~6 저장"],
            ["sp_LoadPlayerExtra", "퀘스트 킬카운트 조회"],
            ["sp_SavePlayerExtra", "퀘스트 킬카운트 저장"],
        ],
        col_widths=[5.0, 10.5]
    )

    add_h2(doc, "3.6 타이머 이벤트 큐")
    add_body(doc, "concurrent_priority_queue<event_type>  — wakeup_time 기준 min-heap")
    add_table(doc,
        ["이벤트", "주기", "설명"],
        [
            ["EVENT_NPC_MOVE",    "500ms (Agro/Boss 250ms)", "NPC 이동 tick"],
            ["EVENT_HP_REGEN",    "5,000ms",                 "플레이어 HP max_hp×10% 회복"],
            ["EVENT_NPC_RESPAWN", "30,000ms / 보스 120,000ms","사망 NPC 부활 (원점 ±80 반경)"],
        ],
        col_widths=[4.5, 4.5, 6.5]
    )

    # ── 4. 게임 흐름 ───────────────────────────────────────
    add_h1(doc, "4. 게임 흐름")
    add_h2(doc, "4.1 로그인")
    add_code(doc,
        "클라이언트           서버                      DB 스레드\n"
        "  ├─ C2S_LOGIN ────>│\n"
        "  │                  ├─ CS_DB_WAIT 상태 전환\n"
        "  │                  ├─ DB_LOGIN 이벤트 push ──>│\n"
        "  │                  │                           ├─ sp_LoginOrCreate\n"
        "  │                  │                           ├─ sp_LoadPlayerExtra\n"
        "  │                  │<── IO_DB_LOGIN IOCP 완료 ─┤\n"
        "  │                  ├─ m_inventory[], m_quests[] 복원\n"
        "  │                  ├─ CS_PLAYING 상태 전환\n"
        "  │<── S2C_LOGIN_RESULT / AVATAR_INFO / ITEM_ADD / QUEST_UPDATE / ADD_PLAYER\n"
        "  (bot_ 접두사 계정: DB 완전 스킵 → 즉시 CS_PLAYING)"
    )

    add_h2(doc, "4.2 이동")
    add_code(doc,
        "C2S_MOVE 수신\n"
        "├─ 쿨타임 체크 (500ms, SPD 버프 시 do_move() 2회 실행)\n"
        "├─ is_obstacle() 장애물 충돌 검사\n"
        "├─ 섹터 갱신 + 월드 아이템 자동 획득 검사\n"
        "└─ update_player_view() → S2C_MOVE_PLAYER 브로드캐스트"
    )

    add_h2(doc, "4.3 전투")
    add_code(doc,
        "A키 (C2S_ATTACK): 쿨타임 1,000ms\n"
        "  └─ 바라보는 방향 앞 1칸 NPC 피격  dmg = calc_atk_dmg(level) [ATK버프 시 ×2]\n"
        "     NPC HP==0 → npc_die() : EXP지급 + 아이템드롭 + 리스폰 예약 + 레벨업 체크\n"
        "\n"
        "D키 (C2S_RANGED_ATTACK): 쿨타임 2,000ms\n"
        "  └─ 바라보는 방향으로 최대 6타일 직선, 첫 번째 NPC 1마리 피격 후 소멸\n"
        "     S2C_PROJECTILE 브로드캐스트 (투사체 시각 효과)\n"
        "\n"
        "S키 (C2S_SKILL): 쿨타임 3,000ms\n"
        "  └─ ±1칸 3×3 전체 NPC 피격  dmg = calc_skill_dmg(level) [ATK버프 시 ×2]\n"
        "\n"
        "NPC 공격: dmg = 8  (DEF버프 활성 시 dmg/2 = 4)"
    )

    add_h2(doc, "4.4 사망 / 부활")
    add_code(doc,
        "플레이어 사망: EXP 50% 감소 → 위치=(1000,100) → HP 전회복\n"
        "NPC 사망     : 섹터 제거 → 30초 후 원점 ±80 랜덤 위치 부활\n"
        "             부활 즉시 인접 정지 플레이어에게 S2C_ADD_PLAYER 전송\n"
        "보스 사망    : 120초 후 고정 스폰 위치 부활"
    )

    add_h2(doc, "4.5 아이템")
    add_table(doc,
        ["슬롯", "아이템", "효과"],
        [
            ["1", "HP 포션",    "HP +30 (상한: max_hp)"],
            ["2", "Hi 포션",    "HP +60 (상한: max_hp)"],
            ["3", "엘릭서",     "HP = max_hp"],
            ["4", "공격력 강화","공격력 2배 30초"],
            ["5", "방어력 강화","피해 절반 30초"],
            ["6", "이동속도 증가","이동 2회/tick 30초"],
        ],
        col_widths=[1.5, 4.0, 10.0]
    )
    add_body(doc, "드롭: Peace 30%, Agro 25~35%, Boss 20~30% (종류별 차등)")
    add_body(doc, "획득: 플레이어 이동 시 같은 타일 자동 획득 → 인벤토리 수량+1")

    add_h2(doc, "4.6 퀘스트")
    add_table(doc,
        ["퀘스트", "목표", "보상"],
        [
            ["Agro Slayer (ID=0)", "Agro NPC 10마리 처치", "500 EXP + 공격력 강화[4] 1개"],
            ["Boss Hunter (ID=1)", "보스 3마리 처치",      "3,000 EXP + 방어력 강화[5] 1개"],
        ],
        col_widths=[4.5, 5.0, 6.0]
    )

    add_h2(doc, "4.7 접속 종료")
    add_code(doc,
        "GQCS bytes=0 감지\n"
        "├─ CS_PLAYING 상태면 DB 저장 큐에 push\n"
        "│   ├─ sp_SavePlayer      (x,y,hp,level,exp,inv1~6)\n"
        "│   └─ sp_SavePlayerExtra (quest_kill[0..1])\n"
        "├─ 섹터에서 제거\n"
        "└─ 시야 내 플레이어에게 S2C_REMOVE_PLAYER 전송"
    )

    # ── 5. 알고리즘 ────────────────────────────────────────
    add_h1(doc, "5. 알고리즘")
    add_h2(doc, "5.1 A* 길찾기")
    add_body(doc, "Agro/Boss NPC가 장애물을 우회하여 플레이어를 추격할 때 사용.")
    add_code(doc,
        "탐색 범위  : NPC-플레이어 바운딩박스 + MARGIN(20)  ≈ 최대 900노드\n"
        "휴리스틱   : 맨해튼 거리\n"
        "이동 방향  : 4방향 (대각선 없음)\n"
        "반환       : 다음 이동할 타일 좌표 1개 (매 tick 재계산, 경로 저장 없음)\n"
        "경로 없음  : 현재 위치 반환 (제자리)"
    )

    add_h2(doc, "5.2 NPC AI 상태 머신")
    add_code(doc,
        "IDLE ──────────────> ROAMING (Agro: 스폰 ±20 랜덤 이동)\n"
        "                         │\n"
        "           플레이어 체비쇼프 ≤ 10 감지\n"
        "                         ↓\n"
        "                      CHASE  (A* 추격 + 근접 공격)\n"
        "                         │\n"
        "           플레이어 이탈 또는 사망\n"
        "                         ↓\n"
        "                     IDLE / ROAMING"
    )
    add_body(doc, "Peace: 이동 없음 (고정). Agro: 로밍+추격. Boss: 감지범위 15, 페이즈별 이동속도.")

    add_h2(doc, "5.3 보스 3페이즈")
    add_table(doc,
        ["페이즈", "HP 조건", "행동", "데미지"],
        [
            ["Phase 1", "HP > 66%",       "일반 근접 (체비쇼프 ≤ 2)", "20"],
            ["Phase 2", "33% < HP ≤ 66%", "빠른 이동 (250ms) + 강타", "30"],
            ["Phase 3", "HP ≤ 33%",       "광역 공격 (반경 3칸)",      "45"],
        ],
        col_widths=[2.5, 4.0, 5.5, 3.5]
    )

    add_h2(doc, "5.4 지형 장애물")
    add_body(doc, "map.lua에서 rect 목록 로드 → g_obstacle_map 비트맵 변환. is_obstacle()로 이동 충돌 검사.")
    add_table(doc,
        ["타입 (obs_type)", "설명", "클라이언트 렌더링"],
        [
            ["0 (벽/바위)", "성채 벽, 기둥, 바위",   "회색/갈색 사각형"],
            ["1 (나무)",    "숲 클러스터",             "육각형 수관(초록) + 기둥(갈색)"],
            ["2 (물)",      "호수, 연못, 강",          "반투명 파란색 + 파도 줄무늬"],
        ],
        col_widths=[3.5, 4.0, 8.0]
    )
    add_body(doc, "바이너리 포맷: magic='OBS2', n=int32, per rect=int16×5 (x,y,w,h,obs_type). 구버전 OBS1 하위호환.")
    add_body(doc, "월드 구성: 성채 100개+, 나무 클러스터 27개, 수역 약 20개 (모서리·통로·내부 연못)")

    add_h2(doc, "5.5 섹터 기반 시야 관리")
    add_code(doc,
        "오브젝트 추가/이동: old_sector → new_sector 이전\n"
        "시야 계산 (update_player_view):\n"
        "  인접 3×3 섹터 내 오브젝트 조회\n"
        "  → 체비쇼프 ≤ VIEW_RANGE(5) 필터\n"
        "  → 이전 시야와 비교 → ADD / REMOVE 패킷 전송\n"
        "  복잡도: O(인접 섹터 오브젝트 수) ≪ O(전체 NPC 수)"
    )

    add_h2(doc, "5.6 DB 비동기 처리")
    add_code(doc,
        "메인(Worker) 스레드         DB 스레드\n"
        "  ├─ db_queue.push(event) ──>│\n"
        "  │                          ├─ ODBC 실행 (sp_LoginOrCreate 등)\n"
        "  │                          └─ PostQueuedCompletionStatus(IO_DB_LOGIN)\n"
        "  │<── IOCP 완료 통보 ────────┤\n"
        "  └─ IO_DB_LOGIN 핸들러에서 세션 초기화\n"
        "\n"
        "concurrent_queue<DB_EVENT>로 락 없이 생산자-소비자 구현.\n"
        "DB 스레드 단일 운영 → DB 연결 1개 재사용."
    )

    add_h2(doc, "5.7 송신 배치 최적화 (Send Batching)")
    add_body(doc, "이동/전투 브로드캐스트 시 패킷 1개마다 WSASend를 호출하면 NPC 20만 마리 환경에서 syscall 폭증.")
    add_code(doc,
        "개선 전: 패킷 N개 → WSASend() × N번 호출\n"
        "개선 후: send_queue에 누적 → 하나의 WSASend로 배치 전송\n"
        "  · SEND_BATCH_SIZE 단위로 m_send_queue를 하나의 버퍼에 병합\n"
        "  · WSASend 완료 후 큐에 남은 패킷이 있으면 자동 재전송\n"
        "  · 동접 5,000명 환경에서 CPU 사용률 및 레이턴시 대폭 감소"
    )

    # ── 6. 주요 수치 ────────────────────────────────────────
    add_h1(doc, "6. 주요 수치 요약")
    add_table(doc,
        ["항목", "값"],
        [
            ["이동 쿨타임",             "500ms"],
            ["공격 쿨타임 (A키)",       "1,000ms"],
            ["원거리 공격 쿨타임 (D키)", "2,000ms"],
            ["스킬 쿨타임 (S키)",       "3,000ms"],
            ["원거리 공격 사거리",       "6타일 (직선, 첫 충돌 시 소멸)"],
            ["HP 회복 주기",            "5,000ms (max_hp × 10%)"],
            ["NPC 부활 시간",           "30,000ms"],
            ["보스 부활 시간",          "120,000ms"],
            ["플레이어 기본 HP", "100 + (레벨-1)×10"],
            ["플레이어 공격력",  "15 + (레벨-1)×2"],
            ["스킬 데미지",      "30 + (레벨-1)×3"],
            ["Agro NPC HP",      "80"],
            ["Peace NPC HP",     "30"],
            ["보스 HP",          "500"],
            ["NPC 공격 데미지",  "8 (DEF 버프 시 4)"],
            ["레벨업 필요 EXP",  "100 × 2^(레벨-1)"],
            ["경험치 공식",      "레벨²×2 (Agro 2배, Boss 10배)"],
            ["버프 지속 시간",   "30초"],
        ],
        col_widths=[5.5, 10.0]
    )

    path = os.path.join(OUT, "게임설명서.docx")
    doc.save(path)
    print(f"[OK] {path}")


# ──────────────────────────────────────────────
#  PPTX  발표자료
# ──────────────────────────────────────────────
from pptx import Presentation
from pptx.util import Inches, Pt as PPt, Emu
from pptx.dml.color import RGBColor as PRGBColor
from pptx.enum.text import PP_ALIGN
from pptx.util import Cm as PCm

NAVY  = PRGBColor(0x1B, 0x2A, 0x4A)
BLUE  = PRGBColor(0x2D, 0x6D, 0xF6)
WHITE = PRGBColor(0xFF, 0xFF, 0xFF)
LGRAY = PRGBColor(0xF0, 0xF2, 0xF5)
DGRAY = PRGBColor(0x55, 0x65, 0x75)
MGRAY = PRGBColor(0xCC, 0xD0, 0xD8)

SW = Inches(13.33)
SH = Inches(7.5)

def new_prs():
    prs = Presentation()
    prs.slide_width  = SW
    prs.slide_height = SH
    return prs

def blank_slide(prs):
    layout = prs.slide_layouts[6]  # blank
    return prs.slides.add_slide(layout)

def add_rect(slide, x, y, w, h, fill=None, line=None, line_w=None):
    from pptx.util import Emu
    shape = slide.shapes.add_shape(1, x, y, w, h)  # MSO_SHAPE_TYPE.RECTANGLE=1
    shape.line.fill.background()
    if fill:
        shape.fill.solid()
        shape.fill.fore_color.rgb = fill
    else:
        shape.fill.background()
    if line:
        shape.line.color.rgb = line
        if line_w:
            shape.line.width = line_w
    else:
        shape.line.fill.background()
    return shape

def add_txbox(slide, text, x, y, w, h,
              size=18, bold=False, color=None, align=PP_ALIGN.LEFT,
              wrap=True, italic=False):
    txb = slide.shapes.add_textbox(x, y, w, h)
    tf  = txb.text_frame
    tf.word_wrap = wrap
    tf.auto_size = None
    p   = tf.paragraphs[0]
    p.alignment = align
    run = p.add_run()
    run.text = text
    run.font.size  = PPt(size)
    run.font.bold  = bold
    run.font.italic = italic
    run.font.name  = "맑은 고딕"
    if color:
        run.font.color.rgb = color
    return txb

def add_bullet_txbox(slide, items, x, y, w, h, size=16, color=None, spacing=1.15):
    from pptx.util import Pt as uPt
    from pptx.oxml.ns import qn
    from lxml import etree
    txb = slide.shapes.add_textbox(x, y, w, h)
    tf  = txb.text_frame
    tf.word_wrap = True
    first = True
    for item in items:
        if first:
            p = tf.paragraphs[0]
            first = False
        else:
            p = tf.add_paragraph()
        p.space_before = uPt(4)
        run = p.add_run()
        run.text = item
        run.font.size = PPt(size)
        run.font.name = "맑은 고딕"
        if color:
            run.font.color.rgb = color
    return txb

def title_bar(slide, text, subtitle=None):
    """슬라이드 상단 네이비 바 + 제목"""
    add_rect(slide, 0, 0, SW, Inches(1.15), fill=NAVY)
    add_txbox(slide, text,
              Inches(0.4), Inches(0.15), Inches(12.0), Inches(0.75),
              size=28, bold=True, color=WHITE, align=PP_ALIGN.LEFT)
    if subtitle:
        add_txbox(slide, subtitle,
                  Inches(0.4), Inches(0.78), Inches(12.0), Inches(0.32),
                  size=14, bold=False, color=PRGBColor(0xB0,0xC4,0xE8), align=PP_ALIGN.LEFT)

def footer(slide, text="게임서버프로그래밍 텀프로젝트  |  2021184018"):
    add_txbox(slide, text,
              Inches(0.3), Inches(7.15), Inches(12.0), Inches(0.28),
              size=10, color=DGRAY, align=PP_ALIGN.LEFT)

def divider(slide, y_inch):
    add_rect(slide, Inches(0.3), Inches(y_inch), Inches(12.7), PPt(1.5), fill=MGRAY)

def info_box(slide, label, value, x, y, w=Inches(2.8), h=Inches(1.1)):
    add_rect(slide, x, y, w, h, fill=LGRAY)
    add_txbox(slide, label, x+PPt(8), y+PPt(8), w-PPt(16), PPt(20), size=11, color=DGRAY)
    add_txbox(slide, value, x+PPt(8), y+PPt(24), w-PPt(16), h-PPt(30), size=22, bold=True, color=NAVY, wrap=False)

def build_pptx():
    prs = new_prs()

    # ── 슬라이드 1: 타이틀 ──────────────────────────────────
    sl = blank_slide(prs)
    add_rect(sl, 0, 0, SW, SH, fill=NAVY)
    add_rect(sl, Inches(0.4), Inches(2.6), Inches(9.0), PPt(3), fill=BLUE)
    add_txbox(sl, "Simplest MMORPG",
              Inches(0.4), Inches(1.2), Inches(12.5), Inches(1.4),
              size=52, bold=True, color=WHITE, align=PP_ALIGN.LEFT)
    add_txbox(sl, "게임서버프로그래밍 텀프로젝트",
              Inches(0.4), Inches(2.75), Inches(9.0), Inches(0.6),
              size=22, bold=False, color=WHITE, align=PP_ALIGN.LEFT)
    add_txbox(sl, "학번: 2021184018",
              Inches(0.4), Inches(3.55), Inches(6.0), Inches(0.5),
              size=16, color=PRGBColor(0xB0,0xC4,0xE8), align=PP_ALIGN.LEFT)
    add_txbox(sl, "IOCP · SFML · MS SQL Server · Lua",
              Inches(0.4), Inches(4.05), Inches(9.0), Inches(0.4),
              size=14, italic=True, color=PRGBColor(0x80,0xA0,0xD0), align=PP_ALIGN.LEFT)

    # 우측 숫자 강조
    for i, (val, lbl) in enumerate([("200,000", "NPCs"), ("5,000+", "동시접속"), ("15", "Boss Zones")]):
        bx = Inches(10.0)
        by = Inches(1.4 + i * 1.7)
        add_rect(sl, bx, by, Inches(2.9), Inches(1.5), fill=PRGBColor(0x25,0x3A,0x5E))
        add_txbox(sl, val, bx+PPt(10), by+PPt(8), Inches(2.7), PPt(38),
                  size=32, bold=True, color=BLUE, align=PP_ALIGN.CENTER)
        add_txbox(sl, lbl, bx+PPt(10), by+PPt(50), Inches(2.7), PPt(22),
                  size=14, color=WHITE, align=PP_ALIGN.CENTER)

    # ── 슬라이드 2: 게임 개요 ───────────────────────────────
    sl = blank_slide(prs)
    title_bar(sl, "게임 개요", "무엇을 만들었나?")
    # 4개 info box
    boxes = [
        ("월드 크기",    "2,000 × 2,000"),
        ("NPC 총 수",    "200,015마리"),
        ("동시접속",     "5,000명 달성"),
        ("NPC 구역",     "15개 Named Zone"),
    ]
    for i, (lbl, val) in enumerate(boxes):
        col, row = i % 2, i // 2
        info_box(sl, lbl, val,
                 x=Inches(0.4 + col * 6.5),
                 y=Inches(1.4 + row * 1.4),
                 w=Inches(6.1), h=Inches(1.2))

    add_txbox(sl, "장르",          Inches(0.4),  Inches(4.45), Inches(2.0), PPt(22), size=13, bold=True, color=NAVY)
    add_txbox(sl, "타일 기반 실시간 MMORPG — 플레이어가 하나의 월드에서 함께 이동·전투·성장",
              Inches(2.2), Inches(4.45), Inches(10.5), PPt(22), size=13, color=DGRAY)
    add_txbox(sl, "서버 스택",     Inches(0.4),  Inches(4.95), Inches(2.0), PPt(22), size=13, bold=True, color=NAVY)
    add_txbox(sl, "Windows IOCP (C++) · Worker thread + Timer thread · ODBC → MS SQL Server",
              Inches(2.2), Inches(4.95), Inches(10.5), PPt(22), size=13, color=DGRAY)
    add_txbox(sl, "클라이언트",    Inches(0.4),  Inches(5.45), Inches(2.0), PPt(22), size=13, bold=True, color=NAVY)
    add_txbox(sl, "SFML 2.5.1 (C++) · Winsock2 TCP · 한국어 UI (맑은 고딕)",
              Inches(2.2), Inches(5.45), Inches(10.5), PPt(22), size=13, color=DGRAY)
    footer(sl)

    # ── 슬라이드 3: 월드 & 섹터 ────────────────────────────
    sl = blank_slide(prs)
    title_bar(sl, "월드 구성", "맵 · 섹터 시스템 · 장애물")

    # 왼쪽: 구조 설명
    items_l = [
        "· 2000×2000 타일맵, 장애물 비트맵 (is_obstacle())",
        "· Lua 스크립트(map.lua)로 장애물 rect 정의 → 서버 로드",
        "· 성채(castle) 100개+  절차적 격자 생성 (step=140)",
        "· 15개 Named Zone — 각 Zone마다 보스 고정 스폰",
        "· 장애물 3종: 벽/바위(0), 나무(1), 물(2)",
        "· 나무 클러스터 27개 + 수역 약 20개 (월드 전역 분산)",
    ]
    add_bullet_txbox(sl, items_l, Inches(0.4), Inches(1.3), Inches(6.3), Inches(2.5),
                     size=15, color=PRGBColor(0x22,0x22,0x22))

    items_r = [
        "섹터 크기  = VIEW_RANGE×2+1 = 11×11",
        "총 섹터 수 = 182×182 = 33,124개",
        "시야 계산  = 인접 3×3 섹터 조회",
        "브로드캐스트 대상 ≪ 전체 NPC 수",
        "→ O(N²) 아닌 O(인접 섹터) 복잡도",
    ]
    bx = Inches(7.0)
    add_rect(sl, bx, Inches(1.25), Inches(5.9), Inches(2.8), fill=LGRAY)
    add_txbox(sl, "섹터 기반 시야 최적화", bx+PPt(10), Inches(1.35), Inches(5.7), PPt(22),
              size=13, bold=True, color=NAVY)
    add_bullet_txbox(sl, items_r, bx+PPt(10), Inches(1.65), Inches(5.7), Inches(2.2),
                     size=13, color=PRGBColor(0x22,0x22,0x22))

    add_txbox(sl, "Zone 목록 (15개)",
              Inches(0.4), Inches(4.0), Inches(12.5), PPt(22),
              size=13, bold=True, color=NAVY)
    zone_txt = ("Boss(중앙) · Orc NW/NE · Goblin SW/SE · Ogre W/E "
                "· Knight NW/NE/SW/SE · Dragon NW/NE/SW/SE")
    add_txbox(sl, zone_txt, Inches(0.4), Inches(4.3), Inches(12.5), PPt(28),
              size=13, color=DGRAY)
    footer(sl)

    # ── 슬라이드 4: NPC 시스템 ──────────────────────────────
    sl = blank_slide(prs)
    title_bar(sl, "NPC 시스템", "20만 마리 · 3종류 · AI 상태머신")

    cols = [
        ("Peace NPC", "100,000마리", ["· 고정 위치", "· 반격 없음", "· HP 30", "· Lv.1~3", "· 처치 EXP 낮음"], PRGBColor(0x2E,0x86,0x48)),
        ("Agro NPC",  "100,000마리", ["· 로밍 ±20", "· 플레이어 추격", "· HP 80", "· Lv.2~6", "· A* 길찾기"], PRGBColor(0xC0,0x39,0x2B)),
        ("Boss NPC",  "15마리",     ["· Zone 중심 고정", "· 3페이즈 패턴", "· HP 500", "· Lv.20", "· 2분 후 부활"], PRGBColor(0x8E,0x44,0xAD)),
    ]
    for i, (title, count, items, color) in enumerate(cols):
        bx = Inches(0.4 + i * 4.3)
        by = Inches(1.3)
        add_rect(sl, bx, by, Inches(4.0), PPt(4), fill=color)
        add_txbox(sl, title, bx+PPt(8), by+PPt(6), Inches(3.8), PPt(24),
                  size=17, bold=True, color=WHITE)
        add_txbox(sl, count, bx+PPt(8), by+PPt(32), Inches(3.8), PPt(18),
                  size=13, color=WHITE)
        add_rect(sl, bx, by+PPt(55), Inches(4.0), Inches(3.5), fill=LGRAY)
        add_bullet_txbox(sl, items, bx+PPt(10), by+PPt(62), Inches(3.8), Inches(3.2),
                         size=14, color=PRGBColor(0x22,0x22,0x22))

    add_txbox(sl, "AI 상태머신:  IDLE  →  ROAMING (Agro: 스폰±20)  →  CHASE (플레이어 체비쇼프≤10 감지, A* 추격)",
              Inches(0.4), Inches(6.55), Inches(12.5), PPt(28), size=13, color=NAVY)
    footer(sl)

    # ── 슬라이드 5: 전투 & 스킬 ─────────────────────────────
    sl = blank_slide(prs)
    title_bar(sl, "전투 & 스킬", "A키 · S키 · 보스 3페이즈 · 레벨업 스탯")

    skills = [
        ("A키  — 방향성 근접",    "쿨타임 1초",  "바라보는 방향 앞 1칸 NPC 피격\n데미지 = 15 + (레벨-1)×2\nD키: 직선 6타일 원거리 (쿨 2초)"),
        ("S키  — 3×3 광역 스킬",  "쿨타임 3초",  "플레이어 중심 ±1칸 전체 피격\n데미지 = 30 + (레벨-1)×3"),
        ("보스 — 3페이즈",        "120초 부활",  "P1: 근접 20pt\nP2: 빠른이동 30pt\nP3: 광역 45pt"),
    ]
    for i, (ttl, cd, desc) in enumerate(skills):
        bx = Inches(0.4 + i * 4.3)
        by = Inches(1.3)
        add_rect(sl, bx, by, Inches(4.0), Inches(1.0), fill=NAVY)
        add_txbox(sl, ttl, bx+PPt(8), by+PPt(6), Inches(3.8), PPt(24),
                  size=15, bold=True, color=WHITE)
        add_txbox(sl, cd,  bx+PPt(8), by+PPt(32), Inches(3.8), PPt(18),
                  size=11, color=PRGBColor(0xB0,0xC4,0xE8))
        add_rect(sl, bx, by+Inches(1.0), Inches(4.0), Inches(2.6), fill=LGRAY)
        add_txbox(sl, desc, bx+PPt(10), by+Inches(1.05), Inches(3.8), Inches(2.4),
                  size=14, color=PRGBColor(0x22,0x22,0x22))

    add_txbox(sl, "레벨업 스탯 스케일링",
              Inches(0.4), Inches(4.85), Inches(12.5), PPt(22),
              size=14, bold=True, color=NAVY)
    scale_items = [
        "· max_hp  = 100 + (레벨-1)×10      레벨업 시 HP 전회복",
        "· 공격력  = 15  + (레벨-1)×2",
        "· 스킬    = 30  + (레벨-1)×3       EXP = 100 × 2^(레벨-1)",
    ]
    add_bullet_txbox(sl, scale_items, Inches(0.4), Inches(5.2), Inches(12.5), Inches(1.5),
                     size=14, color=PRGBColor(0x22,0x22,0x22))
    footer(sl)

    # ── 슬라이드 6: 아이템 & 퀘스트 ────────────────────────
    sl = blank_slide(prs)
    title_bar(sl, "아이템 & 퀘스트", "드롭 · 자동 획득 · 반복 퀘스트")

    # 아이템 표
    add_txbox(sl, "아이템 (6종)",
              Inches(0.4), Inches(1.3), Inches(6.0), PPt(22),
              size=14, bold=True, color=NAVY)
    item_rows = [
        ("HP 포션",     "HP +30"),
        ("Hi 포션",     "HP +60"),
        ("엘릭서",      "HP 전회복"),
        ("공격력 강화", "공격 ×2, 30초"),
        ("방어력 강화", "피해 ÷2, 30초"),
        ("이동속도 증가","2칸/tick, 30초"),
    ]
    for i, (name, eff) in enumerate(item_rows):
        by = Inches(1.7) + i * PPt(32)
        add_rect(sl, Inches(0.4), by, Inches(2.8), PPt(28), fill=LGRAY)
        add_txbox(sl, name, Inches(0.5), by+PPt(5), Inches(2.0), PPt(20), size=12, bold=True, color=NAVY)
        add_txbox(sl, eff,  Inches(2.8), by+PPt(5), Inches(3.3), PPt(20), size=12, color=DGRAY)

    add_txbox(sl, "드롭: NPC 처치 시 확률 드롭 → 같은 타일 이동으로 자동 획득",
              Inches(0.4), Inches(5.0), Inches(6.0), PPt(22), size=12, italic=True, color=DGRAY)

    # 퀘스트
    add_txbox(sl, "퀘스트 (2종)",
              Inches(7.0), Inches(1.3), Inches(6.0), PPt(22),
              size=14, bold=True, color=NAVY)
    quests = [
        ("Agro Slayer", "Agro NPC 10마리 처치", "500 EXP + 공격력 강화"),
        ("Boss Hunter", "보스 3마리 처치",       "3,000 EXP + 방어력 강화"),
    ]
    for i, (qname, goal, reward) in enumerate(quests):
        by = Inches(1.7 + i * 2.0)
        add_rect(sl, Inches(7.0), by, Inches(5.9), Inches(1.7), fill=LGRAY)
        add_txbox(sl, qname,  Inches(7.1), by+PPt(8),  Inches(5.7), PPt(22), size=15, bold=True, color=NAVY)
        add_txbox(sl, "목표: " + goal,  Inches(7.1), by+PPt(34), Inches(5.7), PPt(18), size=12, color=DGRAY)
        add_txbox(sl, "보상: " + reward,Inches(7.1), by+PPt(54), Inches(5.7), PPt(18), size=12, color=BLUE)

    add_txbox(sl, "진행 상태: 우상단 항상 표시 + Q키 상세 패널",
              Inches(7.0), Inches(5.8), Inches(5.9), PPt(22), size=12, italic=True, color=DGRAY)
    footer(sl)

    # ── 슬라이드 7: DB & 저장 구조 ──────────────────────────
    sl = blank_slide(prs)
    title_bar(sl, "DB 연동", "비동기 처리 · 자동 저장/로드")

    add_txbox(sl, "비동기 DB 처리 흐름",
              Inches(0.4), Inches(1.3), Inches(6.0), PPt(22),
              size=14, bold=True, color=NAVY)
    flow = [
        "① C2S_LOGIN 수신 → 세션 CS_DB_WAIT 상태",
        "② db_queue에 DB_LOGIN 이벤트 push",
        "③ DB 스레드: ODBC 실행 (sp_LoginOrCreate)",
        "④ PostQueuedCompletionStatus(IO_DB_LOGIN)",
        "⑤ Worker 스레드: 세션 초기화 → CS_PLAYING",
        "⑥ 접속 종료 시 sp_SavePlayer 자동 저장",
    ]
    add_bullet_txbox(sl, flow, Inches(0.4), Inches(1.7), Inches(6.2), Inches(3.5),
                     size=14, color=PRGBColor(0x22,0x22,0x22))

    add_txbox(sl, "저장 항목",
              Inches(7.0), Inches(1.3), Inches(5.9), PPt(22),
              size=14, bold=True, color=NAVY)
    db_items = [
        ("위치 (x, y)",    "마지막 접속 위치 복원"),
        ("HP",             "현재 HP (레벨로 max_hp 파생)"),
        ("레벨 / EXP",     "레벨업 스탯 자동 적용"),
        ("인벤토리[6]",    "슬롯별 아이템 수량"),
        ("퀘스트 킬카운트","Agro/Boss 킬수 유지"),
    ]
    for i, (field, desc) in enumerate(db_items):
        by = Inches(1.7 + i * 0.75)
        add_txbox(sl, field, Inches(7.0), by, Inches(2.2), PPt(22), size=13, bold=True, color=NAVY)
        add_txbox(sl, desc,  Inches(9.2), by, Inches(3.7), PPt(22), size=13, color=DGRAY)

    add_rect(sl, Inches(0.4), Inches(5.4), Inches(12.5), PPt(36), fill=PRGBColor(0xE8,0xF0,0xFE))
    add_txbox(sl, "bot_ 접두사 계정: DB 완전 스킵 → 기본값으로 즉시 CS_PLAYING (성능 테스트용)",
              Inches(0.6), Inches(5.45), Inches(12.0), PPt(28), size=13, italic=True, color=NAVY)
    footer(sl)

    # ── 슬라이드 8: IOCP 서버 구조 ─────────────────────────
    sl = blank_slide(prs)
    title_bar(sl, "IOCP 서버 구조", "Worker threads · Timer thread · 이벤트 처리")

    # 박스들로 아키텍처 표현
    boxes_arch = [
        (Inches(0.4),  Inches(1.35), Inches(3.5), Inches(1.1),
         "클라이언트 N개",       "TCP 3500\nWSASend / WSARecv", LGRAY, NAVY),
        (Inches(4.5),  Inches(1.35), Inches(4.4), Inches(1.1),
         "IOCP 완료 큐",         "IO_RECV / IO_SEND\nIO_NPC_MOVE / IO_DB_LOGIN", PRGBColor(0xD9,0xE8,0xFF), NAVY),
        (Inches(9.5),  Inches(1.35), Inches(3.4), Inches(1.1),
         "Worker Threads (N개)", "패킷 처리\n섹터/브로드캐스트", PRGBColor(0xD5,0xF5,0xE3), PRGBColor(0x1A,0x5C,0x30)),
        (Inches(0.4),  Inches(3.0),  Inches(3.5), Inches(1.1),
         "Timer Thread",         "priority_queue<event>\nNPC_MOVE / HP_REGEN / RESPAWN", PRGBColor(0xFE,0xF9,0xE7), PRGBColor(0x7D,0x60,0x08)),
        (Inches(4.5),  Inches(3.0),  Inches(4.4), Inches(1.1),
         "DB Thread (1개)",      "concurrent_queue<DB_EVENT>\nODBC → MS SQL Server", PRGBColor(0xFD,0xE8,0xE8), PRGBColor(0x7B,0x24,0x1C)),
        (Inches(9.5),  Inches(3.0),  Inches(3.4), Inches(1.1),
         "Sector Manager",       "33,124개 섹터\nadd / remove / query", PRGBColor(0xED,0xE7,0xF6), PRGBColor(0x4A,0x23,0x5A)),
    ]
    for bx, by, bw, bh, title, desc, bg, tc in boxes_arch:
        add_rect(sl, bx, by, bw, bh, fill=bg)
        add_txbox(sl, title, bx+PPt(8), by+PPt(6),  bw-PPt(16), PPt(22), size=13, bold=True, color=tc)
        add_txbox(sl, desc,  bx+PPt(8), by+PPt(30), bw-PPt(16), bh-PPt(36), size=11, color=DGRAY)

    add_txbox(sl, "이벤트 종류",
              Inches(0.4), Inches(4.4), Inches(12.5), PPt(22),
              size=14, bold=True, color=NAVY)
    ev_items = [
        "EVENT_NPC_MOVE    : NPC 이동 tick (Agro/Boss 500ms, Boss P2/P3 250ms)",
        "EVENT_HP_REGEN    : 플레이어 HP 회복 (5초마다 max_hp×10%)",
        "EVENT_NPC_RESPAWN : 사망 NPC 부활 예약 (일반 30초, 보스 120초)",
        "IO_DB_LOGIN       : DB 조회 완료 → 세션 초기화",
    ]
    add_bullet_txbox(sl, ev_items, Inches(0.4), Inches(4.75), Inches(12.5), Inches(2.0),
                     size=13, color=PRGBColor(0x22,0x22,0x22))
    footer(sl)

    # ── 슬라이드 9: 성능 최적화 ─────────────────────────────
    sl = blank_slide(prs)
    title_bar(sl, "성능 최적화", "Send Batching — 패킷 배치 송신")

    # 왼쪽: Before
    add_rect(sl, Inches(0.4), Inches(1.25), Inches(5.9), PPt(28), fill=PRGBColor(0xC0,0x39,0x2B))
    add_txbox(sl, "Before  (개선 전)",
              Inches(0.5), Inches(1.3), Inches(5.7), PPt(20),
              size=13, bold=True, color=WHITE)
    add_rect(sl, Inches(0.4), Inches(1.7), Inches(5.9), Inches(2.5), fill=LGRAY)
    before_items = [
        "· 패킷 1개마다 WSASend() 호출",
        "· N개 NPC 이동 = N번 syscall",
        "· 20만 NPC × 시야내 플레이어 수",
        "  → 초당 수백만 WSASend 가능",
        "· CPU 사용률 급증 / 레이턴시 불안정",
    ]
    add_bullet_txbox(sl, before_items, Inches(0.5), Inches(1.75), Inches(5.7), Inches(2.3),
                     size=13, color=PRGBColor(0x22,0x22,0x22))

    # 스크린샷 — Before
    IMG_ROOT = os.path.join(os.path.dirname(OUT), "") if OUT.endswith("docs") else OUT + "\\..\\"
    IMG_ROOT = os.path.normpath(os.path.join(OUT, ".."))
    sl.shapes.add_picture(
        os.path.join(IMG_ROOT, "send큐 적용 이전.png"),
        Inches(0.4), Inches(4.3), Inches(5.9), Inches(2.5)
    )

    # 오른쪽: After
    add_rect(sl, Inches(7.0), Inches(1.25), Inches(5.9), PPt(28), fill=PRGBColor(0x1A,0x5C,0x30))
    add_txbox(sl, "After  (개선 후 — Send Batching)",
              Inches(7.1), Inches(1.3), Inches(5.7), PPt(20),
              size=13, bold=True, color=WHITE)
    add_rect(sl, Inches(7.0), Inches(1.7), Inches(5.9), Inches(2.5), fill=LGRAY)
    after_items = [
        "· send_queue에 패킷 누적",
        "· SEND_BATCH_SIZE 단위로 병합",
        "· 하나의 WSASend로 일괄 전송",
        "· IO 완료 후 잔여 패킷 자동 재전송",
        "· syscall 횟수 대폭 감소",
    ]
    add_bullet_txbox(sl, after_items, Inches(7.1), Inches(1.75), Inches(5.7), Inches(2.3),
                     size=13, color=PRGBColor(0x22,0x22,0x22))

    # 스크린샷 — After
    sl.shapes.add_picture(
        os.path.join(IMG_ROOT, "send큐 적용 이후 성능 테스트.png"),
        Inches(7.0), Inches(4.3), Inches(5.9), Inches(2.5)
    )

    footer(sl)

    # ── 슬라이드 10: 동시접속 결과 ─────────────────────────
    sl = blank_slide(prs)
    title_bar(sl, "동시접속 성능 결과", "bot_ 계정 스트레스 테스트")

    info_box(sl, "목표", "5,000명", Inches(0.4), Inches(1.4), Inches(3.0), Inches(1.2))
    info_box(sl, "달성", "5,000명 ✓", Inches(3.8), Inches(1.4), Inches(3.0), Inches(1.2))
    info_box(sl, "테스트 방법", "bot_ 자동 로그인", Inches(7.2), Inches(1.4), Inches(5.7), Inches(1.2))

    add_txbox(sl, "테스트 방법",
              Inches(0.4), Inches(2.9), Inches(12.5), PPt(22),
              size=14, bold=True, color=NAVY)
    test_items = [
        "· bot_XXXX 형식 계정 → DB 스킵, 즉시 게임 진입 (성능 테스트 전용)",
        "· STRESS_TEST 솔루션: 멀티스레드로 N개 봇 클라이언트 동시 접속",
        "· 봇은 맵 전역에 랜덤 분산 → 이동/공격 반복으로 서버 부하 시뮬레이션",
        "· 목표: 5,000 동접 상태에서 서버 크래시 없음 + 정상 패킷 처리 확인",
    ]
    add_bullet_txbox(sl, test_items, Inches(0.4), Inches(3.25), Inches(12.5), Inches(2.0),
                     size=14, color=PRGBColor(0x22,0x22,0x22))

    add_rect(sl, Inches(0.4), Inches(5.55), Inches(12.5), PPt(36), fill=PRGBColor(0xD5,0xF5,0xE3))
    add_txbox(sl, "결과: 동시접속 5,000명 달성 — 서버 안정 운영 확인 (성능 점수 20점 충족)",
              Inches(0.6), Inches(5.6), Inches(12.0), PPt(28), size=14, bold=True, color=PRGBColor(0x1A,0x5C,0x30))
    footer(sl)

    # ── 슬라이드 11: 마무리 ─────────────────────────────────
    sl = blank_slide(prs)
    title_bar(sl, "구현 완료 항목 요약", "기본 구현 + 추가 요소")

    # 2열 체크리스트
    left_items = [
        "✔ IOCP 서버 (Worker + Timer thread)",
        "✔ 플레이어 로그인 / 이동 / 채팅",
        "✔ 섹터 기반 시야 관리",
        "✔ NPC 20만 마리 (Peace / Agro / Boss)",
        "✔ AI 상태머신 + A* 길찾기",
        "✔ 보스 3페이즈 패턴",
        "✔ HP 회복 / 사망 / 부활 처리 (즉시 알림 수정)",
        "✔ 경험치 / 레벨업 / 스탯 스케일링",
    ]
    right_items = [
        "✔ DB 비동기 연동 (저장 / 로드)",
        "✔ 아이템 드롭 · 자동 획득 · 사용",
        "✔ 퀘스트 2종 (킬카운트 + 보상, Q키 패널)",
        "✔ A키 방향성 근접 + D키 원거리 (6타일)",
        "✔ S키 3×3 광역 스킬",
        "✔ Lua 스크립트: 성채 + 나무 + 물 장애물",
        "✔ 미니맵 (M키) · Send Batching 최적화",
        "✔ 동시접속 5,000명 달성",
    ]
    add_bullet_txbox(sl, left_items,  Inches(0.4), Inches(1.3), Inches(6.3), Inches(4.5),
                     size=14, color=PRGBColor(0x22,0x22,0x22))
    add_bullet_txbox(sl, right_items, Inches(7.0), Inches(1.3), Inches(6.2), Inches(4.5),
                     size=14, color=PRGBColor(0x22,0x22,0x22))

    add_rect(sl, Inches(0.4), Inches(6.2), Inches(12.5), PPt(50), fill=NAVY)
    add_txbox(sl, "Q & A",
              Inches(0.4), Inches(6.25), Inches(12.5), PPt(40),
              size=24, bold=True, color=WHITE, align=PP_ALIGN.CENTER)

    path = os.path.join(OUT, "발표자료.pptx")
    prs.save(path)
    print(f"[OK] {path}")


if __name__ == "__main__":
    build_docx()
    build_pptx()
    print("완료.")
