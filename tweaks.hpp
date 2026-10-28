#pragma once

#include "system.hpp"

// ============================================================================
// Твики реестра/системы. Все изменения — через Journal::* (бэкап + откат).
// ============================================================================
void T_AdvancedNetwork();
void T_GameDVR();
void T_MouseAccel();
void T_MouseBuffer();
void T_Priority();
void T_CrossDevice();
void T_SvcHost();
void T_NTFS8_3();
void T_NTFSLast();
void T_AutoMaint();
void T_Defender();
void T_UAC();
void T_SecCenter();
void T_SmartScreen();
void T_Firewall();
void T_AMSI();
void T_CodeInteg();
void T_HVCI();
void T_VBS();
void T_SteamLite();
void T_DisableDynamicTick();
void T_Ultimate();
void T_PowerThrot();
void T_Hibernate();
void T_USBSuspend();
void T_QoS();
void T_IPv6();
void T_Nagle();
void T_RSS();
void T_Checksum();
void T_LSO();
void T_MenuShowDelay();
void T_StartupDelay();
void T_PowerThrottlingGlobal();
bool T_HwSchMode(bool silent = false);
bool T_HwSchModeSilent();
void T_DNS();
void T_Prefetcher();
void T_FSEBehavior();
void T_SysMain();
void T_WSearch();
void T_OneDrive();
void T_Xbox();
void T_Edge();
void T_Teams();
void T_Telemetry();
void T_AdID();
void T_Location();
void T_Spooler();
void T_RemoteReg();
void T_Fax();
void T_WerSvc();
void T_Bluetooth();
void T_NetworkThrottling();
void T_SystemResponsiveness();
void T_DisableTransparency();
void T_EnableGpuMsiMode();
void T_MmcssGamesProfile();
void T_RawInputBoost();
void T_CpuUnparkAndBoost();
void T_DwmMpoFix();
void T_KernelMemoryLock();
void T_NvidiaPowerMax();
void T_NvidiaTelemetryOff();
void T_AmdUlpsOff();
void T_AmdTelemetryOff();
void T_MemoryCompressionToggle();
void T_DisableWidgets();
void T_TcpAutotuning();
void T_DisableGameMode();
void T_DeferUpdates();
void T_TabletInputService();
void T_CloudClipboard();

// ============================================================================
// Fix / восстановление
// ============================================================================
void F_EAC();
void F_Vanguard();
void F_BlueStacks();
void F_BluetoothAudio();
void F_WU();
void F_Store();
bool F_Net();
void F_Icons();
bool F_Dism();
bool F_StoreFix();
void F_DiscordFix();
bool F_AudioFix();
bool F_CompactOS();
void F_FixDiscordShare();
void F_FixFiveMAudio();
void F_FixGta5Crash();
bool F_FixPrintSpooler();
void F_FixXboxServices();
void F_FixWebView2();
void F_RevertDwmTweaks();

// ============================================================================
// Guard 2.0 / состояние твиков
// ============================================================================
bool IsTweakAllowedForHardware(int id);
const wchar_t* GetTweakBlockReason(int id);
bool ReadTweakState(int id);
void LoadTweakStates();
bool VerifyDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD expect);
int  TweakIndexById(int id);
bool IsAntiCheatActive(std::wstring* detectedName = nullptr);

// ============================================================================
// Профили / пакетные применения
// ============================================================================
void ApplyMagicBoost();
void ApplyTurboBoost();
// Пакет #17 (2a.2): тестируемый состав ТУРБО (без побочных эффектов).
std::vector<int> BuildTurboTweakList();
void RevertTweak(int id);
void HandlePowerStateChange(bool onAC);
void InitTweaks();
bool PromptRestoreDnsBeforeExit();

// ============================================================================
// Игровые конфиги
// ============================================================================
void ShowGameConfig(const std::wstring& gameName);
std::wstring FindGameExe(const std::wstring& rootDir, const std::wstring& exeName);
void CreateOptimizedShortcut(const std::wstring& targetExe, const std::wstring& gameName);
void SetGameHighPerformanceGPU(const std::wstring& exeName);
void ApplyGameExeTweaks(const std::wstring& exeName);
void ApplyCS2Config();
void ApplyDota2Config();
void ApplyCS2LaunchOptions();
void ApplyDota2LaunchOptions();
void ApplyArizonaConfig();
void ApplyArizonaLauncherHints();
