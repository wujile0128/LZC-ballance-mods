#ifndef BML_EXPORT
#define BML_EXPORT __declspec(dllexport)
#endif

#include "BML/IMod.h"
#include "BML/IBML.h"
#include "BML/IConfig.h"
#include "BML/ILogger.h"
#include "MusicPlayer.h"
#include <cstdio>
#include <string>
#include <cstring>
#include <functional>
#include <memory>

#include "CKLevel.h"
#include "BML/GUI/Text.h"          // BGui 文本组件

class MusicReplacer : public IMod {
public:
    enum State { IDLE, FADING_IN, PLAYING, FADING_OUT };

    explicit MusicReplacer(IBML* bml) : IMod(bml), m_CurrentLevel(-1),
        m_Volume(80), m_FadeInStep(15), m_FadeOutStep(20),
        m_State(IDLE), m_PendingLevel(-1), m_IsEnding(false),
        m_LoopFadeLeadTime(2000), m_LoopFadePending(false),
        m_ShowProgress(true), m_ProgressX(0.5f), m_ProgressY(0.9f),
        m_FontSize(14), m_NextProgressUpdate(0.0f) {
    }
    ~MusicReplacer() override { StopMusicImmediate(); }

    const char* GetID() override { return "MusicReplacer"; }
    const char* GetVersion() override { return "1.0.3"; }
    const char* GetName() override { return "Music Replacer"; }
    const char* GetAuthor() override { return "LZC"; }
    const char* GetDescription() override { return "Play different music for each level with fade effects."; }
    BMLVersion GetBMLVersion() override { return BMLVersion(); }

    void OnLoad() override {
        auto* logger = GetLogger();
        if (logger) logger->Info("MusicReplacer: OnLoad called.");

        char cwd[256];
        if (GetCurrentDirectoryA(256, cwd)) {
            m_GameRoot = cwd;
            if (m_GameRoot.find("Bin") != std::string::npos) {
                m_GameRoot = m_GameRoot.substr(0, m_GameRoot.find_last_of("\\"));
            }
            if (logger) logger->Info(("MusicReplacer: Game root = " + m_GameRoot).c_str());
        }

        auto* config = GetConfig();
        if (config) {
            config->SetCategoryComment("Music", "Music settings for level background music.");

            auto* folderProp = config->GetProperty("Music", "Folder");
            folderProp->SetComment("Music folder path (relative to game root)");
            folderProp->SetDefaultString("Music");
            m_MusicFolder = folderProp->GetString();
            if (m_MusicFolder.empty()) {
                m_MusicFolder = "Music";
                folderProp->SetString(m_MusicFolder.c_str());
            }

            auto* volProp = config->GetProperty("Music", "Volume");
            volProp->SetComment("Master volume (1-100, default 80)");
            volProp->SetDefaultInteger(80);
            int v = volProp->GetInteger();
            if (v < 1 || v > 100) { v = 80; volProp->SetInteger(v); }
            m_Volume = v;

            auto* inProp = config->GetProperty("Music", "FadeInStep");
            inProp->SetComment("Fade-in speed (0=off, 1-50, higher=faster, default 15)");
            inProp->SetDefaultInteger(15);
            int inStep = inProp->GetInteger();
            if (inStep < 0 || inStep > 50) { inStep = 15; inProp->SetInteger(inStep); }
            m_FadeInStep = inStep;

            auto* outProp = config->GetProperty("Music", "FadeOutStep");
            outProp->SetComment("Fade-out speed (0=off, 1-50, higher=faster, default 20)");
            outProp->SetDefaultInteger(20);
            int outStep = outProp->GetInteger();
            if (outStep < 0 || outStep > 50) { outStep = 20; outProp->SetInteger(outStep); }
            m_FadeOutStep = outStep;

            auto* leadProp = config->GetProperty("Music", "LoopFadeLead");
            leadProp->SetComment("Loop fade lead time (0=off, 500-10000, default 2000)");
            leadProp->SetDefaultInteger(2000);
            int lead = leadProp->GetInteger();
            if (lead < 0 || lead > 10000) { lead = 2000; leadProp->SetInteger(lead); }
            m_LoopFadeLeadTime = lead;

            auto* showProp = config->GetProperty("Music", "ShowProgress");
            showProp->SetComment("Show music progress overlay");
            showProp->SetDefaultBoolean(true);
            m_ShowProgress = showProp->GetBoolean();

            auto* xProp = config->GetProperty("Music", "ProgressX");
            xProp->SetComment("Progress text X position (0-1)");
            xProp->SetDefaultFloat(0.5f);
            m_ProgressX = xProp->GetFloat();

            auto* yProp = config->GetProperty("Music", "ProgressY");
            yProp->SetComment("Progress text Y position (0-1)");
            yProp->SetDefaultFloat(0.9f);
            m_ProgressY = yProp->GetFloat();

            auto* fontProp = config->GetProperty("Music", "FontSize");
            fontProp->SetComment("Font size of progress text");
            fontProp->SetDefaultFloat(14.0f);
            m_FontSize = (int)fontProp->GetFloat();

            if (logger) {
                logger->Info(("MusicReplacer: Final config - Folder=" + m_MusicFolder +
                    ", Volume=" + std::to_string(m_Volume) +
                    ", FadeInStep=" + std::to_string(m_FadeInStep) +
                    ", FadeOutStep=" + std::to_string(m_FadeOutStep) +
                    ", LoopFadeLead=" + std::to_string(m_LoopFadeLeadTime)).c_str());
                logger->Info(("MusicReplacer: Progress - Show=" + std::to_string(m_ShowProgress) +
                    ", X=" + std::to_string(m_ProgressX) +
                    ", Y=" + std::to_string(m_ProgressY) +
                    ", FontSize=" + std::to_string(m_FontSize)).c_str());
            }
        }
        else {
            m_MusicFolder = "Music";
            m_Volume = 80;
            m_FadeInStep = 15;
            m_FadeOutStep = 20;
            m_LoopFadeLeadTime = 2000;
        }

        m_CurrentLevel = -1;
        m_State = IDLE;
        m_PendingLevel = -1;
        m_IsEnding = false;
        m_LoopFadePending = false;
        m_CurrentMusicPath.clear();

        m_BML->AddTimerLoop((CKDWORD)200, [this]() -> bool {
            CheckMusicLoop();
            return true;
            });
        if (logger) logger->Info("MusicReplacer: Loop check timer started (200ms).");
    }

    void OnUnload() override {
        auto* logger = GetLogger();
        if (logger) logger->Info("MusicReplacer: OnUnload called.");
        StopMusicImmediate();
        DestroyProgressText();
        m_CurrentLevel = -1;
        m_State = IDLE;
        m_LoopFadePending = false;
        m_IsEnding = false;
    }

    void OnModifyConfig(const char* category, const char* key, IProperty* prop) override {
        auto* logger = GetLogger();
        if (logger) logger->Info(("MusicReplacer: Config modified: " + std::string(category) + " / " + std::string(key)).c_str());

        if (strcmp(category, "Music") == 0) {
            if (strcmp(key, "Folder") == 0) {
                m_MusicFolder = prop->GetString();
                if (m_MusicFolder.empty()) m_MusicFolder = "Music";
            }
            else if (strcmp(key, "Volume") == 0) {
                int v = prop->GetInteger();
                m_Volume = (v > 0 && v <= 100) ? v : 80;
                if (m_State == PLAYING) MusicPlayer::SetVolume(m_Volume * 10);
            }
            else if (strcmp(key, "FadeInStep") == 0) {
                int s = prop->GetInteger();
                m_FadeInStep = (s >= 0 && s <= 50) ? s : 15;
            }
            else if (strcmp(key, "FadeOutStep") == 0) {
                int s = prop->GetInteger();
                m_FadeOutStep = (s >= 0 && s <= 50) ? s : 20;
            }
            else if (strcmp(key, "LoopFadeLead") == 0) {
                int l = prop->GetInteger();
                m_LoopFadeLeadTime = (l >= 0 && l <= 10000) ? l : 2000;
            }
            else if (strcmp(key, "ShowProgress") == 0 ||
                strcmp(key, "ProgressX") == 0 ||
                strcmp(key, "ProgressY") == 0 ||
                strcmp(key, "FontSize") == 0) {
                ReloadProgressConfig();
            }
        }
    }

    void OnLoadObject(const char* filename, CKBOOL isMap, const char* masterName, CK_CLASSID filterClass,
        CKBOOL addToScene, CKBOOL reuseMeshes, CKBOOL reuseMaterials, CKBOOL dynamic,
        XObjectArray* objArray, CKObject* masterObj) override {
        if (!isMap) return;
        auto* logger = GetLogger();
        if (logger) logger->Info(("MusicReplacer: OnLoadObject - " + std::string(filename)).c_str());

        const char* p = strstr(filename, "Level_");
        if (p) {
            int levelNum = 0;
            if (sscanf(p + 6, "%d", &levelNum) == 1 && levelNum >= 1 && levelNum <= 13) {
                if (levelNum != m_CurrentLevel) {
                    m_CurrentLevel = levelNum;
                    m_BML->AddTimer((CKDWORD)500, [this, levelNum]() {
                        if (m_State == IDLE || m_State == PLAYING)
                            SwitchToLevel(levelNum);
                        else
                            m_PendingLevel = levelNum;
                        });
                }
            }
        }
    }

    // ================== 直接停止，无淡出，避免崩溃 ==================
    void OnPreEndLevel() override {
        if (m_State == FADING_OUT) return;
        auto* logger = GetLogger();
        if (logger) logger->Info("MusicReplacer: OnPreEndLevel triggered - stopping music immediately.");
        StopMusicImmediate();
        DestroyProgressText();
        m_IsEnding = true;
        m_LoopFadePending = false;
        m_State = IDLE;

        if (m_PendingLevel != -1) {
            int lvl = m_PendingLevel;
            m_PendingLevel = -1;
            SwitchToLevel(lvl);
        }
    }

    void OnPreExitLevel() override {
        auto* logger = GetLogger();
        if (logger) logger->Info("MusicReplacer: OnPreExitLevel triggered - stopping music immediately.");
        StopMusicImmediate();
        DestroyProgressText();
        m_IsEnding = true;
        m_LoopFadePending = false;
        m_State = IDLE;
    }

    void OnPostStartMenu() override {
        auto* logger = GetLogger();
        if (logger) logger->Info("MusicReplacer: OnPostStartMenu triggered - immediate stop.");
        m_LoopFadePending = false;
        m_IsEnding = false;
        StopMusicImmediate();
        m_State = IDLE;
        m_CurrentLevel = -1;
        m_PendingLevel = -1;
        m_CurrentMusicPath.clear();
        DestroyProgressText();
    }

    void OnPreResetLevel() override {
        if (m_IsEnding && m_CurrentLevel != -1) {
            m_IsEnding = false;
            StopMusicImmediate();
            SwitchToLevel(m_CurrentLevel);
            auto* logger = GetLogger();
            if (logger) logger->Info("MusicReplacer: Reset level, music restarted.");
        }
        if (m_ShowProgress && !m_ProgressText) {
            CreateProgressText();
        }
    }

    void OnStartLevel() override {
        if (m_ShowProgress && !m_ProgressText) {
            CreateProgressText();
        }
        auto* tm = m_BML->GetTimeManager();
        if (tm) m_NextProgressUpdate = tm->GetTime();
    }

    void OnPauseLevel() override {}
    void OnDead() override {}
    void OnGameOver() override {}

    void OnProcess() override {
        if (!m_ShowProgress || !m_ProgressText)
            return;

        auto* tm = m_BML->GetTimeManager();
        if (!tm) return;
        float currentTime = tm->GetTime();

        if (currentTime >= m_NextProgressUpdate) {
            UpdateProgressText();
            float diff = currentTime - m_NextProgressUpdate;
            if (diff >= 10000.0f)
                m_NextProgressUpdate = currentTime;
            m_NextProgressUpdate += 1000.0f;
        }
    }

private:
    // ================== 进度文本管理 ==================
    void CreateProgressText() {
        if (m_ProgressText) return;
        if (!m_ShowProgress) return;

        m_ProgressText = std::make_unique<BGui::Text>("MusicProgress");
        m_ProgressText->SetSize({ 0.5f, 0.05f });
        m_ProgressText->SetPosition({ m_ProgressX, m_ProgressY });
        m_ProgressText->SetAlignment(CKSPRITETEXT_CENTER);
        m_ProgressText->SetTextColor(0xFFFFFFFF);
        m_ProgressText->SetZOrder(128);
        m_ProgressText->SetFont("Arial", m_FontSize, 400, false, false);
        m_ProgressText->SetText("00:00 / 00:00");

        auto* tm = m_BML->GetTimeManager();
        if (tm) m_NextProgressUpdate = tm->GetTime();
    }

    void DestroyProgressText() {
        m_ProgressText.reset();
    }

    void UpdateProgressText() {
        if (!m_ProgressText) return;

        if (m_CurrentLevel == -1 || m_State != PLAYING) {
            m_ProgressText->SetText("--:-- / --:--");
            return;
        }

        int pos = MusicPlayer::GetPosition();
        int len = MusicPlayer::GetLength();
        if (len <= 0 || pos < 0) {
            m_ProgressText->SetText("--:-- / --:--");
            return;
        }

        char buf[64];
        int posMin = pos / 60000;
        int posSec = (pos % 60000) / 1000;
        int lenMin = len / 60000;
        int lenSec = (len % 60000) / 1000;
        snprintf(buf, sizeof(buf), "%02d:%02d / %02d:%02d", posMin, posSec, lenMin, lenSec);
        m_ProgressText->SetText(buf);
    }

    void ReloadProgressConfig() {
        auto* config = GetConfig();
        if (!config) return;

        auto* showProp = config->GetProperty("Music", "ShowProgress");
        if (showProp) m_ShowProgress = showProp->GetBoolean();

        auto* xProp = config->GetProperty("Music", "ProgressX");
        if (xProp) m_ProgressX = xProp->GetFloat();

        auto* yProp = config->GetProperty("Music", "ProgressY");
        if (yProp) m_ProgressY = yProp->GetFloat();

        auto* fontProp = config->GetProperty("Music", "FontSize");
        if (fontProp) m_FontSize = (int)fontProp->GetFloat();

        DestroyProgressText();
        if (m_ShowProgress) {
            CreateProgressText();
        }
    }

    // ================== 循环检测 ==================
    void CheckMusicLoop() {
        if (m_IsEnding) return;
        if (m_CurrentLevel == -1) return;
        if (m_PendingLevel != -1) return;
        if (m_State == FADING_IN || m_State == FADING_OUT) return;
        if (m_LoopFadePending) return;
        if (m_CurrentMusicPath.empty()) return;

        if (m_LoopFadeLeadTime == 0) {
            if (!MusicPlayer::IsPlaying()) {
                MusicPlayer::Stop();
                if (MusicPlayer::Play(m_CurrentMusicPath)) {
                    MusicPlayer::SetVolume(m_Volume * 10);
                    m_State = PLAYING;
                    auto* logger = GetLogger();
                    if (logger) logger->Info("MusicReplacer: Loop hard restart (no fade).");
                }
                else {
                    m_State = IDLE;
                    auto* logger = GetLogger();
                    if (logger) logger->Error("MusicReplacer: Failed to restart loop music.");
                }
            }
            return;
        }

        if (MusicPlayer::IsPlaying()) {
            int pos = MusicPlayer::GetPosition();
            int len = MusicPlayer::GetLength();
            if (len > 0 && pos > 0) {
                int remaining = len - pos;
                if (remaining < m_LoopFadeLeadTime && remaining > 0) {
                    auto* logger = GetLogger();
                    if (logger) logger->Info(("MusicReplacer: Loop fade-out triggered, remaining " + std::to_string(remaining) + " ms").c_str());
                    m_LoopFadePending = true;
                    m_State = FADING_OUT;
                    StopMusicWithFade([this]() {
                        MusicPlayer::Stop();
                        if (!m_CurrentMusicPath.empty() && MusicPlayer::Play(m_CurrentMusicPath)) {
                            m_State = FADING_IN;
                            StartFadeIn([this]() {
                                m_State = PLAYING;
                                m_LoopFadePending = false;
                                auto* log = GetLogger();
                                if (log) log->Info("MusicReplacer: Loop fade-in complete, music restarted.");
                                });
                        }
                        else {
                            m_State = IDLE;
                            m_LoopFadePending = false;
                            auto* log = GetLogger();
                            if (log) log->Error("MusicReplacer: Failed to restart loop music.");
                        }
                        });
                }
            }
        }
        else {
            if (m_CurrentMusicPath.empty()) return;
            auto* logger = GetLogger();
            if (logger) logger->Info("MusicReplacer: Music stopped unexpectedly, restarting with fade-in.");
            m_LoopFadePending = true;
            MusicPlayer::Stop();
            if (MusicPlayer::Play(m_CurrentMusicPath)) {
                m_State = FADING_IN;
                StartFadeIn([this]() {
                    m_State = PLAYING;
                    m_LoopFadePending = false;
                    });
            }
            else {
                m_State = IDLE;
                m_LoopFadePending = false;
            }
        }
    }

    // ================== 核心切换 ==================
    void SwitchToLevel(int level) {
        m_IsEnding = false;
        m_PendingLevel = -1;
        if (m_State == FADING_IN || m_State == FADING_OUT) {
            m_PendingLevel = level;
            return;
        }
        auto* logger = GetLogger();

        char path[512];
        snprintf(path, sizeof(path), "%s\\%s\\Level_%02d.mp3", m_GameRoot.c_str(), m_MusicFolder.c_str(), level);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
            if (logger) logger->Error(("MusicReplacer: File does NOT exist: " + std::string(path)).c_str());
            return;
        }

        m_CurrentMusicPath = path;
        m_LoopFadePending = false;

        bool isPlaying = MusicPlayer::IsPlaying();
        int currentVol = MusicPlayer::GetVolume();

        if (isPlaying && currentVol > 0) {
            m_State = FADING_OUT;
            StopMusicWithFade([this, path]() {
                MusicPlayer::Stop();
                if (MusicPlayer::Play(path)) {
                    m_State = FADING_IN;
                    StartFadeIn([this]() {
                        m_State = PLAYING;
                        auto* log = GetLogger();
                        if (log) log->Info("MusicReplacer: Fade-in complete, now playing.");
                        });
                }
                else {
                    m_State = IDLE;
                    auto* log = GetLogger();
                    if (log) log->Error("MusicReplacer: Failed to play new music.");
                }
                });
        }
        else {
            MusicPlayer::Stop();
            if (MusicPlayer::Play(path)) {
                m_State = FADING_IN;
                StartFadeIn([this]() {
                    m_State = PLAYING;
                    auto* log = GetLogger();
                    if (log) log->Info("MusicReplacer: Fade-in complete, now playing.");
                    });
            }
            else {
                m_State = IDLE;
                auto* log = GetLogger();
                if (log) log->Error("MusicReplacer: Failed to play music.");
            }
        }
    }

    // ================== 渐变控制 ==================
    void StartFadeIn(std::function<void()> onComplete) {
        MusicPlayer::SetVolume(0);
        FadeStep(0, m_Volume * 10, m_FadeInStep, 30, onComplete);
    }

    void StopMusicWithFade(std::function<void()> onComplete) {
        int currentVol = MusicPlayer::GetVolume();
        if (currentVol <= 0) {
            MusicPlayer::Stop();
            if (onComplete) onComplete();
            return;
        }
        FadeStep(currentVol, 0, -m_FadeOutStep, 30, [this, onComplete]() {
            MusicPlayer::Stop();
            if (onComplete) onComplete();
            });
    }

    void StopMusicImmediate() {
        MusicPlayer::Stop();
        m_State = IDLE;
        m_LoopFadePending = false;
    }

    void FadeStep(int current, int target, int step, int interval, std::function<void()> onComplete) {
        if (step == 0) {
            MusicPlayer::SetVolume(target);
            if (onComplete) onComplete();
            return;
        }

        int newVol = current + step;
        bool reached = false;
        if ((step > 0 && newVol >= target) || (step < 0 && newVol <= target)) {
            newVol = target;
            reached = true;
        }
        MusicPlayer::SetVolume(newVol);
        if (reached) {
            if (onComplete) onComplete();
            return;
        }
        m_BML->AddTimer((CKDWORD)interval, [this, newVol, target, step, interval, onComplete]() {
            FadeStep(newVol, target, step, interval, onComplete);
            });
    }

    // ================== 成员变量 ==================
    std::string m_MusicFolder;
    std::string m_GameRoot;
    std::string m_CurrentMusicPath;
    int m_CurrentLevel;
    int m_PendingLevel;
    State m_State;
    int m_Volume;
    int m_FadeInStep;
    int m_FadeOutStep;
    bool m_IsEnding;
    int m_LoopFadeLeadTime;
    bool m_LoopFadePending;

    bool m_ShowProgress;
    float m_ProgressX;
    float m_ProgressY;
    int m_FontSize;
    float m_NextProgressUpdate;
    std::unique_ptr<BGui::Text> m_ProgressText;
};

// ================== DLL 入口 ==================
extern "C" BML_EXPORT IMod* BMLEntry(IBML* bml) {
    return new MusicReplacer(bml);
}

extern "C" BML_EXPORT void BMLExit(IMod* mod) {
    delete mod;
}