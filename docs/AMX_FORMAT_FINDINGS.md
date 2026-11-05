# Van Buren scripts (`.scr`) -- Pawn/"Small" bytecode, decoded

83 scripts (`Scripts/*.scr`), run by the engine's `Script_Small` (an embedded Pawn/Small AMX, `amx_Exec` at ~0x5cd0e4). Parser/disassembler/verifier: `amx_parse.py`; C++ VM: `src/assets/amx.{h,cpp}`
(`AmxScript` = loaded program, `AmxVM` = per-entity instance); smoke test `build/vb_amx_test` (runs every public of every script with stubbed natives: 978/991 calls finish cleanly; the other 13 are
`OnAttacked` handlers that read a fifth argument the test does not pass).

## File layout (AMX file version 6, amx version 6, flags 2 = debug info present, `defsize` 40)
```
i32 size | u16 magic 0xF1E0 | u8 file_ver 6 | u8 amx_ver 6 | i16 flags | i16 defsize
i32 cod, dat, hea, stp, cip(-1), publics, natives, libraries, pubvars, tags      (52-byte header; no nametable field)
publics[], natives[]: defsize (40) bytes each = u32 address + char name[36]       (names embedded)
code: 32-bit cells, uncompacted, from `cod` to `dat`;  data: `dat`..`hea` (initial globals/strings); `stp` = total data+heap+stack size
```
* Code starts with `HALT 0` at address 0: the host pushes the arguments, the argument byte count and return address **0**, so `RETN` from a public ends in that HALT and returns PRI.
* The code stream is full of debug records: `FILE size ord "path"`, `LINE n file`, `SYMBOL size addr flags "name"` (locals/globals), `SRANGE a b`; they are skipped by length (`FILE`/`SYMBOL`: opcode, size in bytes, then `size` bytes).
  Source paths show the originals lived at `C:\Code\Script3\...\*.sma` (Small source), e.g. `Player\Player.sma`.
* Calling convention: args pushed right-to-left (`PUSH.C`/`PUSH.S`), then `PUSH.C nbytes`, then `CALL`/`SYSREQ.C native` ; natives read `args[0..n-1]` from the stack, result in PRI, caller cleans up with `STACK`.

## Opcode numbering (verified against the game's own interpreter)
The standard Pawn list with `PUSH_R` inserted after `PUSH_ALT` (`PUSH.R n` pushes PRI n times) and `SWAP_PRI SWAP_ALT PUSHADDR NOP` appended: 1 LOAD_PRI .. 134 NOP (table in `amx_parse.py` / `amx.cpp`).
Checked two ways: (1) all 83 scripts disassemble completely (279,551 instructions) with zero violations -- every public is at a `PROC`, every `CALL` targets a `PROC`, every jump/case target is an
instruction boundary, switch operands point at `CASETBL`s, native indices are in range; (2) the engine's interpreter dispatch (`jbe` on `op-1 <= 0x85` into the 146-entry jump table at 0x5ce0c4) was compared
handler by handler: all 134 handlers match my names/operand counts. Division is floored (remainder takes the divisor's sign) as in Pawn.

## Scripts and their interface (what the game needs from the host)
* 53 distinct public functions. Entity hooks (in nearly every script): `OnCreate OnLoad OnUpdate OnMessage OnDamage OnAttacked OnDeath OnDestroy GetUsableSkills UseSkill ShowSkills`; doors/containers:
  `OnOpen OnClose`; areas `OnEnter OnLeave`; dialogue scripts: `OnStartDialogue` and node functions named like `LA1_R1_N1`, `LC1_R1_N1`, `NU1_N1`, `NA1`, `N1` (see the dialogue notes).
* **109 distinct natives** across all scripts (the engine API to implement): entity: `Myself Protagonist GetObject FindObjects FindEntity MatchEntity GetType GetTeam IsDead GetPosition GetFacing SetFacing SetFacingTarget
  Distance PointDistance GetLOS GetLOSPoint IsEntityInArea GetDirectionToEntity IsIdle SetModel DestroyEntity ActivateEntity DeactivateEntity`; stats: `GetCurrentHP GetMaxHP AddHP SetValue GetRace GetGender GetPrimaryAttribute
  GetTotalSkill GetUsableSkills`; actions: `ClearActions A_MoveToPosition A_MoveToEntity A_Wait A_Wander A_Attack A_JustAttack A_PlayAnim A_SendMessage A_FloatingSpeechText A_Kill A_ReloadWeapon`; teams/squads:
  `CreateTeam AddToTeam GetTeamRank SetTeamRank GetTeamAttitude SetTeamAttitude CreateSquad AddToSquad GetSquadID GetSquadRank SetSquadRank GetSquadAttitude SetSquadAttitude FindTeamMembers SendMessageToTeam`; messages:
  `SendMessage MessageArgCount MessageGetArg`; world: `GetGlobal SetGlobal Hour Minute SecondsPassed GetTimeStamp GetMapID GetWayPointCount GetWayPointPosition GetNearestPath GetNavPointCount GetNavPointPosition
  MapEffectSetActive MapEffectResetElapsedTime AttachVEG RemoveVEG GetVEGID AttachSound RemoveSound IsUnderFOW`; doors/items: `LockDoor UnlockDoor IsLocked HasItem IsItemEquipped EquipItem AddEntry`; text/UI:
  `DisplayStringRef FloatingDamageText FloatingSpeechText PCSpeaker`; dialogue: `StartDialogue IsInDialogue SetCurrentNodeName SetNodeTextFunction RegisterReply OpenTask CloseTask`; math (Small float lib):
  `float floatadd floatsub floatmul floatdiv floatcmp floatround floatfract`; `RandomNumber`.
