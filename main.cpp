#include <ncursesw/ncurses.h>
#include <clocale>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <ctime>
#include <chrono>
#include <cstdlib>
#include <queue>
#include <fstream>
#include <cmath>
#include <cstdint>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>

using namespace std;
using namespace std::chrono;

// MAZE ESCAPE - ANCIENT MYSTERY DUNGEON
// C++17 + NCURSESW

constexpr int MAP_H = 19;
constexpr int MAP_W = 39;
constexpr int MAP_Y = 5;
constexpr int MAP_X = 2;
constexpr int TILE_W = 2;
constexpr int TOTAL_LEVELS = 3;

constexpr int SIDE_X = MAP_X + MAP_W * TILE_W + 4;

mt19937 rng(static_cast<unsigned int>(time(nullptr)));

enum ColorID {
    C_WALL = 1,
    C_PLAYER,
    C_CRYSTAL,
    C_COIN,
    C_KEY,
    C_EXIT,
    C_ENEMY,
    C_TRAP,
    C_TITLE,
    C_TEXT,
    C_DIM,
    C_HP,
    C_PANEL,
    C_SUCCESS,
    C_WARNING
};

struct Position {
    int y, x;
};

struct Pickup {
    int y, x;
    char type;
    bool taken = false;
};

struct Enemy {
    int y, x;
    bool hunter;
};

struct Trap {
    int y, x;
    bool activated = false;
};

struct Game {
    vector<string> maze;
    vector<Pickup> pickups;
    vector<Enemy> enemies;
    vector<Trap> traps;

    int playerY = 1;
    int playerX = 1;

    int hp = 5;
    int maxHP = 5;

    int score = 0;
    int coins = 0;
    int crystals = 0;
    int requiredCrystals = 3;

    int level = 1;
    int difficulty = 2;

    bool hasKey = false;

    string message = "Explore the ancient dungeon!";

    steady_clock::time_point lastEnemyMove;
    steady_clock::time_point lastDamage;
};

// RANDOM
int randomInt(int minimum, int maximum) {
    uniform_int_distribution<int> dist(minimum, maximum);
    return dist(rng);
}

void shuffleVector(vector<int>& values) {
    shuffle(values.begin(), values.end(), rng);
}

bool insideMap(int y, int x) {
    return y > 0 && y < MAP_H - 1 &&
           x > 0 && x < MAP_W - 1;
}

int distanceBetween(int y1, int x1, int y2, int x2) {
    return abs(y1 - y2) + abs(x1 - x2);
}

// AUDIO SYSTEM - REAL WAV AUDIO FOR WSL
const string AUDIO_DIR = "/tmp/maze_escape_audio/";

pid_t menuMusicPID = -1;

void writeWav(const string& filename,
              const vector<pair<double, int>>& notes,
              double volume = 0.35) {

    const int sampleRate = 22050;
    vector<int16_t> samples;

    for (const auto& note : notes) {
        double frequency = note.first;
        int duration = note.second;

        int count = sampleRate * duration / 1000;

        for (int i = 0; i < count; i++) {
            double sample = 0;

            if (frequency > 0) {
                double t = static_cast<double>(i) / sampleRate;

                double envelope = min(1.0, i / 300.0);
                envelope *= min(1.0, (count - i) / 500.0);

                sample = sin(2.0 * M_PI * frequency * t)
                         * volume * envelope;
            }

            samples.push_back(
                static_cast<int16_t>(sample * 32767)
            );
        }
    }

    ofstream file(filename, ios::binary);

    if (!file) return;

    uint32_t dataSize = samples.size() * sizeof(int16_t);
    uint32_t fileSize = 36 + dataSize;
    uint16_t audioFormat = 1;
    uint16_t channels = 1;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    uint16_t blockAlign = channels * bitsPerSample / 8;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<char*>(&fileSize), 4);
    file.write("WAVE", 4);

    file.write("fmt ", 4);

    uint32_t subchunk1Size = 16;

    file.write(reinterpret_cast<char*>(&subchunk1Size), 4);
    file.write(reinterpret_cast<char*>(&audioFormat), 2);
    file.write(reinterpret_cast<char*>(&channels), 2);
    file.write(reinterpret_cast<char*>(const_cast<int*>(&sampleRate)), 4);
    file.write(reinterpret_cast<char*>(&byteRate), 4);
    file.write(reinterpret_cast<char*>(&blockAlign), 2);
    file.write(reinterpret_cast<char*>(&bitsPerSample), 2);

    file.write("data", 4);
    file.write(reinterpret_cast<char*>(&dataSize), 4);

    file.write(
        reinterpret_cast<char*>(samples.data()),
        dataSize
    );

    file.close();
}

void createAudioFiles() {

    mkdir(AUDIO_DIR.c_str(), 0777);

    // OPENING SOUND
    writeWav(AUDIO_DIR + "opening.wav", {
        {392, 180}, {523, 180}, {659, 250},
        {784, 350}, {0, 100}, {659, 180},
        {784, 450}
    }, 0.45);

    // MENU BACKGROUND MUSIC
    writeWav(AUDIO_DIR + "menu.wav", {
        {523, 300}, {659, 300}, {784, 450},
        {659, 300}, {523, 450}, {0, 120},
        {440, 300}, {523, 300}, {659, 450},
        {587, 300}, {523, 450}, {0, 120},
        {392, 300}, {523, 300}, {659, 450},
        {523, 300}, {440, 450}, {0, 120},
        {392, 300}, {440, 300}, {523, 600},
        {0, 300}
    }, 0.16);

    // ITEM COLLECTION
    writeWav(AUDIO_DIR + "collect.wav", {
        {880, 90}, {1175, 120}, {1568, 180}
    }, 0.45);

    // MONSTER ATTACK
    writeWav(AUDIO_DIR + "monster.wav", {
        {180, 180}, {120, 250}, {85, 350}
    }, 0.65);

    // TRAP
    writeWav(AUDIO_DIR + "trap.wav", {
        {950, 70}, {250, 180}, {120, 350}
    }, 0.60);

    // LEVEL COMPLETE
    writeWav(AUDIO_DIR + "level.wav", {
        {523, 160}, {659, 160}, {784, 160},
        {1046, 350}
    }, 0.45);

    // VICTORY
    writeWav(AUDIO_DIR + "victory.wav", {
        {523, 180}, {659, 180}, {784, 180},
        {1046, 250}, {1175, 250}, {1318, 500}
    }, 0.50);
}

void playWav(const string& filename) {

    while (waitpid(-1, nullptr, WNOHANG) > 0) {}

    pid_t pid = fork();

    if (pid == 0) {
        execlp(
            "paplay",
            "paplay",
            filename.c_str(),
            (char*)nullptr
        );

        _exit(127);
    }
}

void playEffect(const string& filename) {
    playWav(AUDIO_DIR + filename);
}

void startMenuMusic() {

    if (menuMusicPID > 0) return;

    menuMusicPID = fork();

    if (menuMusicPID == 0) {

        setpgid(0, 0);

        execl(
            "/bin/sh",
            "sh",
            "-c",
            "while true; do paplay /tmp/maze_escape_audio/menu.wav >/dev/null 2>&1; done",
            (char*)nullptr
        );

        _exit(127);
    }

    if (menuMusicPID > 0) {
        setpgid(menuMusicPID, menuMusicPID);
    }
}

void stopMenuMusic() {

    if (menuMusicPID > 0) {
        kill(-menuMusicPID, SIGTERM);
        waitpid(menuMusicPID, nullptr, 0);
        menuMusicPID = -1;
    }
}

void playSound(int type) {

    switch (type) {

        case 1:
            playEffect("collect.wav");
            break;

        case 2:
            playEffect("monster.wav");
            break;

        case 3:
            playEffect("level.wav");
            break;

        case 4:
            playEffect("victory.wav");
            break;

        case 5:
            playEffect("trap.wav");
            break;
    }
}

// COLORS 
void initializeColors() {
    if (!has_colors()) return;

    start_color();
    use_default_colors();

    init_pair(C_WALL, COLOR_BLUE, COLOR_BLUE);
    init_pair(C_PLAYER, COLOR_YELLOW, -1);
    init_pair(C_CRYSTAL, COLOR_CYAN, -1);
    init_pair(C_COIN, COLOR_YELLOW, -1);
    init_pair(C_KEY, COLOR_MAGENTA, -1);
    init_pair(C_EXIT, COLOR_GREEN, -1);
    init_pair(C_ENEMY, COLOR_RED, -1);
    init_pair(C_TRAP, COLOR_RED, -1);
    init_pair(C_TITLE, COLOR_CYAN, -1);
    init_pair(C_TEXT, COLOR_WHITE, -1);
    init_pair(C_DIM, COLOR_BLUE, -1);
    init_pair(C_HP, COLOR_RED, -1);
    init_pair(C_PANEL, COLOR_CYAN, -1);
    init_pair(C_SUCCESS, COLOR_GREEN, -1);
    init_pair(C_WARNING, COLOR_MAGENTA, -1);
}

// DRAWING
void centerText(int y, const string& text) {
    int x = max(0, (COLS - static_cast<int>(text.size())) / 2);
    mvprintw(y, x, "%s", text.c_str());
}

void printColored(int y, int x, int color, const string& text) {
    attron(COLOR_PAIR(color) | A_BOLD);
    mvprintw(y, x, "%s", text.c_str());
    attroff(COLOR_PAIR(color) | A_BOLD);
}

void drawEmoji(int y, int x, const wchar_t* emoji, int color) {
    if (y < 0 || y >= LINES || x < 0 || x + 1 >= COLS) return;

    attron(COLOR_PAIR(color) | A_BOLD);
    mvaddwstr(y, x, emoji);
    attroff(COLOR_PAIR(color) | A_BOLD);
}

void drawFrame(int y, int x, int h, int w, int color) {
    if (h < 2 || w < 2) return;
    if (y < 0 || x < 0 || y + h > LINES || x + w > COLS) return;

    attron(COLOR_PAIR(color) | A_BOLD);

    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);

    for (int i = 1; i < w - 1; i++) {
        mvaddch(y, x + i, ACS_HLINE);
        mvaddch(y + h - 1, x + i, ACS_HLINE);
    }

    for (int i = 1; i < h - 1; i++) {
        mvaddch(y + i, x, ACS_VLINE);
        mvaddch(y + i, x + w - 1, ACS_VLINE);
    }

    attroff(COLOR_PAIR(color) | A_BOLD);
}

// LOGO
void drawLogo() {
    const vector<string> logo = {
        " __  __    _    _____ _____   _____ ____   ____    _    ____  _____ ",
        "|  \\/  |  / \\  |__  /| ____| | ____/ ___| / ___|  / \\  |  _ \\| ____|",
        "| |\\/| | / _ \\   / / |  _|   |  _| \\___ \\| |     / _ \\ | |_) |  _|",
        "| |  | |/ ___ \\ / /_ | |___  | |___ ___) | |___ / ___ \\|  __/| |___",
        "|_|  |_/_/   \\_/____||_____| |_____|____/ \\____/_/   \\_|_|   |_____|"
    };

    int startY = 3;
    int logoWidth = 0;

     for (const string& line : logo) {
        logoWidth = max(logoWidth, (int)line.length());
    }

    int startX = max(0, (COLS - logoWidth) / 2);

    attron(COLOR_PAIR(C_TITLE) | A_BOLD);

    for (int i = 0; i < (int)logo.size(); i++) {
        mvprintw(startY + i, startX, "%s", logo[i].c_str());
    }

    attroff(COLOR_PAIR(C_TITLE) | A_BOLD);

    int centerX = COLS / 2;

    const string subtitle = "A N C I E N T   M Y S T E R Y   D U N G E O N";

int subtitleX = (COLS - static_cast<int>(subtitle.length())) / 2;

mvprintw(9, subtitleX, "%s", subtitle.c_str());
}
