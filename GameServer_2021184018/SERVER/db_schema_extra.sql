-- ================================================================
-- 2021184018_TermProjServer  --  아이템 인벤토리 / 퀘스트 테이블
-- 실행: SSMS에서 2021184018_TermProjServer DB 선택 후 전체 실행
-- ================================================================
USE [2021184018_TermProjServer];
GO

-- ----------------------------------------------------------------
-- 1. player_inventory
--    login_id + slot(1~6)  →  item_count
-- ----------------------------------------------------------------
IF NOT EXISTS (
    SELECT 1 FROM sys.tables WHERE name = 'player_inventory'
)
CREATE TABLE dbo.player_inventory (
    login_id   NVARCHAR(20) NOT NULL,
    slot       TINYINT      NOT NULL,   -- 1~6
    item_count INT          NOT NULL CONSTRAINT DF_inv_count DEFAULT 0,
    CONSTRAINT PK_player_inventory PRIMARY KEY (login_id, slot),
    CONSTRAINT FK_inv_player
        FOREIGN KEY (login_id) REFERENCES dbo.Players(login_id)
        ON DELETE CASCADE
);
GO

-- ----------------------------------------------------------------
-- 2. player_quests
--    login_id + quest_id(0=Agro Slayer / 1=Boss Hunter)  →  kill_count
-- ----------------------------------------------------------------
IF NOT EXISTS (
    SELECT 1 FROM sys.tables WHERE name = 'player_quests'
)
CREATE TABLE dbo.player_quests (
    login_id   NVARCHAR(20) NOT NULL,
    quest_id   TINYINT      NOT NULL,   -- 0 or 1
    kill_count INT          NOT NULL CONSTRAINT DF_quest_kill DEFAULT 0,
    CONSTRAINT PK_player_quests PRIMARY KEY (login_id, quest_id),
    CONSTRAINT FK_quest_player
        FOREIGN KEY (login_id) REFERENCES dbo.Players(login_id)
        ON DELETE CASCADE
);
GO

-- ----------------------------------------------------------------
-- 3. sp_LoadPlayerExtra
--    Result set 1 : 인벤토리 6행 (slot, item_count)
--    Result set 2 : 퀘스트  2행 (quest_id, kill_count)
--    DB에 행이 없는 슬롯/퀘스트는 0으로 채워서 항상 6+2행 반환
-- ----------------------------------------------------------------
IF OBJECT_ID('dbo.sp_LoadPlayerExtra', 'P') IS NOT NULL
    DROP PROCEDURE dbo.sp_LoadPlayerExtra;
GO

CREATE PROCEDURE dbo.sp_LoadPlayerExtra
    @login_id NVARCHAR(20)
AS
BEGIN
    SET NOCOUNT ON;

    -- 인벤토리: 슬롯 1~6, 없으면 0
    SELECT s.slot,
           ISNULL(i.item_count, 0) AS item_count
    FROM (VALUES(1),(2),(3),(4),(5),(6)) AS s(slot)
    LEFT JOIN dbo.player_inventory i
           ON i.login_id = @login_id
          AND i.slot     = s.slot
    ORDER BY s.slot;

    -- 퀘스트: quest_id 0~1, 없으면 0
    SELECT q.quest_id,
           ISNULL(pq.kill_count, 0) AS kill_count
    FROM (VALUES(0),(1)) AS q(quest_id)
    LEFT JOIN dbo.player_quests pq
           ON pq.login_id = @login_id
          AND pq.quest_id = q.quest_id
    ORDER BY q.quest_id;
END
GO

-- ----------------------------------------------------------------
-- 4. sp_SavePlayerExtra
--    인벤토리 6슬롯 + 퀘스트 2개를 MERGE로 UPSERT
-- ----------------------------------------------------------------
IF OBJECT_ID('dbo.sp_SavePlayerExtra', 'P') IS NOT NULL
    DROP PROCEDURE dbo.sp_SavePlayerExtra;
GO

CREATE PROCEDURE dbo.sp_SavePlayerExtra
    @login_id  NVARCHAR(20),
    @inv1      INT,
    @inv2      INT,
    @inv3      INT,
    @inv4      INT,
    @inv5      INT,
    @inv6      INT,
    @q0_kill   INT,
    @q1_kill   INT
AS
BEGIN
    SET NOCOUNT ON;

    -- 인벤토리 UPSERT
    DECLARE @inv TABLE (slot TINYINT, cnt INT);
    INSERT INTO @inv VALUES
        (1, @inv1), (2, @inv2), (3, @inv3),
        (4, @inv4), (5, @inv5), (6, @inv6);

    MERGE dbo.player_inventory AS tgt
    USING @inv AS src
       ON tgt.login_id = @login_id AND tgt.slot = src.slot
    WHEN MATCHED THEN
        UPDATE SET tgt.item_count = src.cnt
    WHEN NOT MATCHED BY TARGET THEN
        INSERT (login_id, slot, item_count)
        VALUES (@login_id, src.slot, src.cnt);

    -- 퀘스트 UPSERT
    DECLARE @quests TABLE (quest_id TINYINT, kill_count INT);
    INSERT INTO @quests VALUES (0, @q0_kill), (1, @q1_kill);

    MERGE dbo.player_quests AS tgt
    USING @quests AS src
       ON tgt.login_id = @login_id AND tgt.quest_id = src.quest_id
    WHEN MATCHED THEN
        UPDATE SET tgt.kill_count = src.kill_count
    WHEN NOT MATCHED BY TARGET THEN
        INSERT (login_id, quest_id, kill_count)
        VALUES (@login_id, src.quest_id, src.kill_count);
END
GO

-- ----------------------------------------------------------------
-- 확인 쿼리 (선택)
-- ----------------------------------------------------------------
-- SELECT * FROM dbo.player_inventory;
-- SELECT * FROM dbo.player_quests;
-- EXEC dbo.sp_LoadPlayerExtra @login_id = N'Alice';
-- EXEC dbo.sp_SavePlayerExtra N'Alice', 3,1,0,1,0,0, 7,2;
-- EXEC dbo.sp_LoadPlayerExtra @login_id = N'Alice';
