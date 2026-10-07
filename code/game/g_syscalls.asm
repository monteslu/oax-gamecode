code

equ	trap_Printf				-1
equ	trap_Error				-2
equ	trap_Milliseconds		-3
equ	trap_Cvar_Register		-4
equ	trap_Cvar_Update		-5
equ	trap_Cvar_Set			-6
equ	trap_Cvar_VariableIntegerValue	-7
equ	trap_Cvar_VariableStringBuffer	-8
equ	trap_Argc				-9
equ	trap_Argv				-10
equ	trap_FS_FOpenFile		-11
equ	trap_FS_Read			-12
equ	trap_FS_Write			-13
equ	trap_FS_FCloseFile		-14
equ	trap_SendConsoleCommand	-15
equ	trap_LocateGameData		-16
equ	trap_DropClient			-17
equ	trap_SendServerCommand	-18
equ	trap_SetConfigstring	-19
equ	trap_GetConfigstring	-20
equ	trap_GetUserinfo		-21
equ	trap_SetUserinfo		-22
equ	trap_GetServerinfo		-23
equ	trap_SetBrushModel		-24
equ	trap_Trace				-25
equ	trap_PointContents		-26
equ trap_InPVS				-27
equ	trap_InPVSIgnorePortals	-28
equ	trap_AdjustAreaPortalState	-29
equ	trap_AreasConnected		-30
equ	trap_LinkEntity			-31
equ	trap_UnlinkEntity		-32
equ	trap_EntitiesInBox		-33
equ	trap_EntityContact		-34
equ	trap_BotAllocateClient	-35
equ	trap_BotFreeClient		-36
equ	trap_GetUsercmd			-37
equ	trap_GetEntityToken		-38
equ	trap_FS_GetFileList		-39
equ trap_DebugPolygonCreate	-40
equ trap_DebugPolygonDelete	-41
equ trap_RealTime			-42
equ trap_SnapVector			-43
equ trap_TraceCapsule		-44
equ trap_EntityContactCapsule	-45
equ trap_FS_Seek -46

equ	memset					-101
equ	memcpy					-102
equ	strncpy					-103
equ	sin						-104
equ	cos						-105
equ	atan2					-106
equ	sqrt					-107
equ floor					-111
equ	ceil					-112
equ	testPrintInt			-113
equ	testPrintFloat			-114



equ trap_BotLibSetup					-201
equ trap_BotLibShutdown					-202
equ trap_BotLibVarSet					-203
equ trap_BotLibVarGet					-204
equ trap_BotLibDefine					-205
equ trap_BotLibStartFrame				-206
equ trap_BotLibLoadMap					-207
equ trap_BotLibUpdateEntity				-208
equ trap_BotLibTest						-209

equ trap_BotGetSnapshotEntity			-210
equ trap_BotGetServerCommand		-211
equ trap_BotUserCommand					-212



equ trap_AAS_EnableRoutingArea		-301
equ trap_AAS_BBoxAreas				-302
equ trap_AAS_AreaInfo				-303
equ trap_AAS_EntityInfo					-304

equ trap_AAS_Initialized				-305
equ trap_AAS_PresenceTypeBoundingBox	-306
equ trap_AAS_Time						-307

equ trap_AAS_PointAreaNum				-308
equ trap_AAS_TraceAreas					-309

equ trap_AAS_PointContents				-310
equ trap_AAS_NextBSPEntity				-311
equ trap_AAS_ValueForBSPEpairKey		-312
equ trap_AAS_VectorForBSPEpairKey		-313
equ trap_AAS_FloatForBSPEpairKey		-314
equ trap_AAS_IntForBSPEpairKey			-315

equ trap_AAS_AreaReachability			-316

equ trap_AAS_AreaTravelTimeToGoalArea	-317

equ trap_AAS_Swimming					-318
equ trap_AAS_PredictClientMovement		-319



equ trap_EA_Say							-401
equ trap_EA_SayTeam						-402
equ trap_EA_Command						-403

equ trap_EA_Action						-404
equ trap_EA_Gesture						-405
equ trap_EA_Talk						-406
equ trap_EA_Attack						-407
equ trap_EA_Use							-408
equ trap_EA_Respawn						-409
equ trap_EA_Crouch						-410
equ trap_EA_MoveUp						-411
equ trap_EA_MoveDown					-412
equ trap_EA_MoveForward					-413
equ trap_EA_MoveBack					-414
equ trap_EA_MoveLeft					-415
equ trap_EA_MoveRight					-416

equ trap_EA_SelectWeapon				-417
equ trap_EA_Jump						-418
equ trap_EA_DelayedJump					-419
equ trap_EA_Move						-420
equ trap_EA_View						-421

equ trap_EA_EndRegular					-422
equ trap_EA_GetInput					-423
equ trap_EA_ResetInput					-424



equ trap_BotLoadCharacter				-501
equ trap_BotFreeCharacter				-502
equ trap_Characteristic_Float			-503
equ trap_Characteristic_BFloat			-504
equ trap_Characteristic_Integer			-505
equ trap_Characteristic_BInteger		-506
equ trap_Characteristic_String			-507

equ trap_BotAllocChatState				-508
equ trap_BotFreeChatState				-509
equ trap_BotQueueConsoleMessage			-510
equ trap_BotRemoveConsoleMessage		-511
equ trap_BotNextConsoleMessage			-512
equ trap_BotNumConsoleMessages			-513
equ trap_BotInitialChat					-514
equ trap_BotReplyChat					-515
equ trap_BotChatLength					-516
equ trap_BotEnterChat					-517
equ trap_StringContains					-518
equ trap_BotFindMatch					-519
equ trap_BotMatchVariable				-520
equ trap_UnifyWhiteSpaces				-521
equ trap_BotReplaceSynonyms				-522
equ trap_BotLoadChatFile				-523
equ trap_BotSetChatGender				-524
equ trap_BotSetChatName					-525

equ trap_BotResetGoalState				-526
equ trap_BotResetAvoidGoals				-527
equ trap_BotPushGoal					-528
equ trap_BotPopGoal						-529
equ trap_BotEmptyGoalStack				-530
equ trap_BotDumpAvoidGoals				-531
equ trap_BotDumpGoalStack				-532
equ trap_BotGoalName					-533
equ trap_BotGetTopGoal					-534
equ trap_BotGetSecondGoal				-535
equ trap_BotChooseLTGItem				-536
equ trap_BotChooseNBGItem				-537
equ trap_BotTouchingGoal				-538
equ trap_BotItemGoalInVisButNotVisible	-539
equ trap_BotGetLevelItemGoal			-540
equ trap_BotAvoidGoalTime				-541
equ trap_BotInitLevelItems				-542
equ trap_BotUpdateEntityItems			-543
equ trap_BotLoadItemWeights				-544
equ trap_BotFreeItemWeights				-546
equ trap_BotSaveGoalFuzzyLogic			-546
equ trap_BotAllocGoalState				-547
equ trap_BotFreeGoalState				-548

equ trap_BotResetMoveState				-549
equ trap_BotMoveToGoal					-550
equ trap_BotMoveInDirection				-551
equ trap_BotResetAvoidReach				-552
equ trap_BotResetLastAvoidReach			-553
equ trap_BotReachabilityArea			-554
equ trap_BotMovementViewTarget			-555
equ trap_BotAllocMoveState				-556
equ trap_BotFreeMoveState				-557
equ trap_BotInitMoveState				-558

equ trap_BotChooseBestFightWeapon		-559
equ trap_BotGetWeaponInfo				-560
equ trap_BotLoadWeaponWeights			-561
equ trap_BotAllocWeaponState			-562
equ trap_BotFreeWeaponState				-563
equ trap_BotResetWeaponState			-564
equ trap_GeneticParentsAndChildSelection -565
equ trap_BotInterbreedGoalFuzzyLogic	-566
equ trap_BotMutateGoalFuzzyLogic		-567
equ trap_BotGetNextCampSpotGoal			-568
equ trap_BotGetMapLocationGoal			-569
equ trap_BotNumInitialChats				-570
equ trap_BotGetChatMessage				-571
equ trap_BotRemoveFromAvoidGoals		-572
equ trap_BotPredictVisiblePosition		-573
equ trap_BotSetAvoidGoalTime			-574
equ trap_BotAddAvoidSpot				-575
equ trap_AAS_AlternativeRouteGoals		-576
equ trap_AAS_PredictRoute				-577
equ trap_AAS_PointReachabilityAreaIndex	-578

equ trap_BotLibLoadSource				-579
equ trap_BotLibFreeSource				-580
equ trap_BotLibReadToken				-581
equ trap_BotLibSourceFileAndLine		-582
 

; oax engine extensions (oax_public.h): number N is equ -(N+1)
equ trap_OAX_DebugSet					-1001
equ trap_OAX_BSPXRead					-1002
equ trap_OAX_GuiLoad					-1041
equ trap_OAX_GuiFree					-1042
equ trap_OAX_GuiSetState				-1043
equ trap_OAX_GuiGetState				-1044
equ trap_OAX_GuiHandleEvent				-1045
equ trap_OAX_GuiTrace					-1046
equ trap_OAX_GuiActivate				-1047
equ trap_OAX_GuiNamedEvent				-1048
equ trap_OAX_GuiStateInfo				-1049
equ trap_OAX_NavStatus					-1091
equ trap_OAX_NavFindPath				-1092
equ trap_OAX_NavNearest					-1093
equ trap_OAX_NavRandomPoint				-1094
equ trap_OAX_NavAddLink					-1095
equ trap_OAX_NavAddArea					-1096
equ trap_OAX_NavCommit					-1097
equ trap_OAX_NavFindPathEx				-1098
equ trap_OAX_NavAddBlocker				-1099
equ trap_OAX_NavSetBlocker				-1100
equ trap_OAX_NavAddModel				-1111
equ trap_OAX_NavSetModel				-1112
equ trap_OAX_EntSetOBB					-1101
equ trap_OAX_ScriptInit					-1011
equ trap_OAX_ScriptRegisterEvent		-1012
equ trap_OAX_ScriptCompileFile			-1013
equ trap_OAX_ScriptSetEntity			-1014
equ trap_OAX_ScriptStartThread			-1015
equ trap_OAX_ScriptRun					-1016
equ trap_OAX_ScriptReturn				-1017
equ trap_OAX_ScriptObjectDone			-1018
equ trap_OAX_ScriptKillThread			-1019
equ trap_OAX_ScriptShutdown				-1020
equ trap_OAX_ScriptNumThreads			-1021

; physics (bg_oax_phys.h): block 1200-1299, the same numbers in game and cgame
equ trap_Phys_WorldCreate                 -1201
equ trap_Phys_WorldDestroy                -1202
equ trap_Phys_WorldStep                   -1203
equ trap_Phys_WorldAddBSP                 -1204
equ trap_Phys_WorldAddHeightField         -1205
equ trap_Phys_WorldSetGravity             -1206
equ trap_Phys_WorldStats                  -1207
equ trap_Phys_WorldHash                   -1208
equ trap_Phys_WorldExplode                -1209
equ trap_Phys_WorldContactEvents          -1210
equ trap_Phys_BodyCreate                  -1211
equ trap_Phys_BodyDestroy                 -1212
equ trap_Phys_BodyAddShape                -1213
equ trap_Phys_BodySetTransform            -1214
equ trap_Phys_BodySetVelocity             -1215
equ trap_Phys_BodyApply                   -1216
equ trap_Phys_BodySetTarget               -1217
equ trap_Phys_BodySetParam                -1218
equ trap_Phys_BodyGetState                -1219
equ trap_Phys_BodyGetStates               -1220
equ trap_Phys_BodyFromBSPModel            -1221
equ trap_Phys_BodyGetMass                 -1222
equ trap_Phys_RagdollCreate               -1223
equ trap_Phys_JointCreate                 -1231
equ trap_Phys_JointDestroy                -1232
equ trap_Phys_JointSetParam               -1233
equ trap_Phys_JointGetParam               -1234
equ trap_Phys_Raycast                     -1241
equ trap_Phys_RaycastBatch                -1242
equ trap_Phys_Shapecast                   -1243
equ trap_Phys_Overlap                     -1244
equ trap_Phys_VehicleCreate               -1261
equ trap_Phys_VehicleDestroy              -1262
equ trap_Phys_VehicleSetInput             -1263
equ trap_Phys_VehicleGetState             -1264
equ trap_Phys_WorldAddTerrain             -1265
equ trap_Phys_VehicleSetState             -1266
