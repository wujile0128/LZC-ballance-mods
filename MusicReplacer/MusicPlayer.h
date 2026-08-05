#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <string>
#include <cstring>
#include <taglib/taglib.h>
#include <taglib/fileref.h>
#include <taglib/audioproperties.h>

#pragma comment(lib, "winmm.lib")

class MusicPlayer {
private:
    // 使用静态局部变量存储当前文件路径，避免外部定义
    static std::string& CurrentFilePath() {
        static std::string path;
        return path;
    }

public:
    static bool Play(const std::string& filePath, bool loop = false) {
        Stop();
        if (GetFileAttributesA(filePath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
        CurrentFilePath() = filePath;

        std::string cmd = "open \"" + filePath + "\" alias bgm";
        if (mciSendStringA(cmd.c_str(), NULL, 0, NULL) != 0) {
            cmd = "open \"" + filePath + "\" type mpegvideo alias bgm";
            if (mciSendStringA(cmd.c_str(), NULL, 0, NULL) != 0)
                return false;
        }
        mciSendStringA("set bgm time format milliseconds", NULL, 0, NULL);
        SetVolume(0);
        std::string playCmd = "play bgm" + std::string(loop ? " repeat" : "");
        return mciSendStringA(playCmd.c_str(), NULL, 0, NULL) == 0;
    }

    static void Stop() {
        mciSendStringA("stop bgm", NULL, 0, NULL);
        mciSendStringA("close bgm", NULL, 0, NULL);
        CurrentFilePath().clear();
    }

    static void Pause() { mciSendStringA("pause bgm", NULL, 0, NULL); }
    static void Resume() { mciSendStringA("resume bgm", NULL, 0, NULL); }
    static void TogglePause() {
        char buf[64] = { 0 };
        mciSendStringA("status bgm mode", buf, sizeof(buf), NULL);
        if (strcmp(buf, "playing") == 0) Pause();
        else if (strcmp(buf, "paused") == 0) Resume();
    }

    static void SetVolume(int vol) {
        if (vol < 0) vol = 0;
        if (vol > 1000) vol = 1000;
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "setaudio bgm volume to %d", vol);
        mciSendStringA(cmd, NULL, 0, NULL);
    }

    static int GetVolume() {
        char buf[64] = { 0 };
        mciSendStringA("status bgm volume", buf, sizeof(buf), NULL);
        if (strlen(buf) == 0) return -1;
        return atoi(buf);
    }

    static int GetPosition() {
        mciSendStringA("set bgm time format milliseconds", NULL, 0, NULL);
        char buf[64] = { 0 };
        mciSendStringA("status bgm position", buf, sizeof(buf), NULL);
        int pos = atoi(buf);
        int len = GetLength();
        if (len > 60000 && pos < 60) {
            pos *= 1000;
        }
        return pos;
    }

    // ================== 使用 TagLib 精确获取时长（毫秒） ==================
    static int GetLength() {
        const std::string& path = CurrentFilePath();
        if (path.empty()) {
            // 未播放时回退到 MCI
            mciSendStringA("set bgm time format milliseconds", NULL, 0, NULL);
            char buf[64] = { 0 };
            mciSendStringA("status bgm length", buf, sizeof(buf), NULL);
            int len = atoi(buf);
            if (len > 0 && len < 60) {
                int candidate = len * 1000;
                if (candidate < 3600000) len = candidate;
            }
            return len;
        }

        // 使用 TagLib 精确扫描所有帧，并直接获取毫秒值
        TagLib::FileRef file(path.c_str(), true, TagLib::AudioProperties::Accurate);
        if (!file.isNull() && file.audioProperties()) {
            // 优先使用 lengthInMilliseconds()（精确到毫秒）
            int ms = file.audioProperties()->lengthInMilliseconds();
            if (ms > 0) return ms;

            // 降级方案：用秒数转换
            int seconds = file.audioProperties()->length();
            if (seconds > 0) return seconds * 1000;
        }

        // 回退到 MCI
        mciSendStringA("set bgm time format milliseconds", NULL, 0, NULL);
        char buf[64] = { 0 };
        mciSendStringA("status bgm length", buf, sizeof(buf), NULL);
        int len = atoi(buf);
        if (len > 0 && len < 60) {
            int candidate = len * 1000;
            if (candidate < 3600000) len = candidate;
        }
        return len;
    }

    static bool IsPlaying() {
        char buf[64] = { 0 };
        mciSendStringA("status bgm mode", buf, sizeof(buf), NULL);
        return strcmp(buf, "playing") == 0;
    }

    static bool IsStopped() {
        char buf[64] = { 0 };
        mciSendStringA("status bgm mode", buf, sizeof(buf), NULL);
        return strcmp(buf, "stopped") == 0;
    }
};