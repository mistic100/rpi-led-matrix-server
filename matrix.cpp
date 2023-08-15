#include <ctime>
#include <csignal>
#include <string.h>
#include <unistd.h>
#include <linux/limits.h>
#include <sys/inotify.h>
#include <sys/time.h>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <dirent.h>
#include <Magick++.h>
#include <magick/image.h>
#include <wiringPi.h>
#include "led-matrix.h"
#include "content-streamer.h"
#include "graphics.h"
#include "lib8tion.h"
#include "Button.h"

#define INOTIFY_BUF_LEN sizeof(struct inotify_event) + NAME_MAX + 1

using namespace std;
using namespace rgb_matrix;

typedef int64_t tmillis_t;

#define IMAGE_DELAY 5000
#define MATRIX_WIDTH 64
#define MATRIX_HEIGHT 32

const char* IMAGES_FOLDER = "./images/";
const string IMAGE_EXTENSION_PNG = ".png";
const string IMAGE_EXTENSION_GIF = ".gif";

const char* NUMS_FILE = "nums.png";
#define NUMS_WIDTH 11
#define NUMS_HEIGHT 12
#define NUMS_SPACE -2

#define SHOW_TIME
#define TIME_X ((MATRIX_WIDTH - (NUMS_WIDTH + NUMS_SPACE) * 5) / 2)
#define TIME_Y ((MATRIX_HEIGHT - NUMS_HEIGHT) / 2)

#define BUTTON_PIN 6 // GPIO25

volatile bool interrupt_received = false;
static void InterruptHandler(int signo)
{
    interrupt_received = true;
}

/**
 * Get current time in milliseconds
 */
tmillis_t GetTimeInMillis() {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

/**
 * Sleep for X milliseconds
 */
void SleepMillis(tmillis_t milli_seconds) {
    if (milli_seconds <= 0) return;
    struct timespec ts;
    ts.tv_sec = milli_seconds / 1000;
    ts.tv_nsec = (milli_seconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

/**
 * Test if a string ends with a suffix
 */
bool endsWith(std::string const &str, std::string const &suffix)
{
    if (str.length() < suffix.length())
    {
        return false;
    }
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

/**
 * Execute a command and returns the result (first line)
 */
string exec(const string &cmd)
{
    array<char, 128> buffer{};
    string result;
    unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe)
    {
        return string();
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    {
        result += buffer.data();
    }
    auto newLine = result.find('\n');
    if (newLine > 0)
    {
        result = result.substr(0, newLine);
    }
    return result;
}

/**
 * List images in the directory
 */
void listImages(vector<string> &images_filenames)
{
    auto start_t = GetTimeInMillis();
    cout << "Read files" << endl;
    images_filenames.clear();

    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(IMAGES_FOLDER)) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            auto name = string(IMAGES_FOLDER) + string(ent->d_name);
            if (endsWith(name, IMAGE_EXTENSION_PNG) || endsWith(name, IMAGE_EXTENSION_GIF))
            {
                images_filenames.emplace_back(name);
                cout << "- " << name << endl;
            }
        }
        closedir(dir);

        sort(images_filenames.begin(), images_filenames.end());

        cout << to_string(images_filenames.size()) << " files listed in " << to_string(GetTimeInMillis() - start_t) << "ms" << endl;
    }
    else
    {
        cerr << "Folder read error" << endl;
    }
}

/**
 * Copy a pixel from an image to a frame
 */
void copyPixel(const Magick::Image &from, FrameCanvas *to,
               int x1, int y1,
               int x2, int y2)
{
    const Magick::Color &c = from.pixelColor(x1, y1);
    if (c.alphaQuantum() < 256)
    {
        to->SetPixel(x2, y2,
                     ScaleQuantumToChar(c.redQuantum()),
                     ScaleQuantumToChar(c.greenQuantum()),
                     ScaleQuantumToChar(c.blueQuantum()));
    }
}

/**
 * Store a frame in the matrix stream
 */
static void storeInStream(const Magick::Image &img, int delay_time_us,
                          rgb_matrix::FrameCanvas *scratch,
                          rgb_matrix::StreamWriter *output) {
    scratch->Clear();
    for (size_t y = 0; y < img.rows(); ++y)
    {
        for (size_t x = 0; x < img.columns(); ++x)
        {
            copyPixel(img, scratch, x, y, x, y);
        }
    }
    output->Stream(*scratch, delay_time_us);
}

/**
 * Loads all images in a matrix stream
 */
rgb_matrix::StreamIO* loadImages(vector<string> &images_filenames, FrameCanvas *offscreen_canvas)
{
    auto start_t = GetTimeInMillis();
    cout << "Load images" << endl;

    rgb_matrix::StreamIO* stream = new rgb_matrix::MemStreamIO();
    rgb_matrix::StreamWriter out(stream);

    uint8_t i = 0;
    for (const auto &filename : images_filenames)
    {
        try
        {
            std::vector<Magick::Image> frames;
            readImages(&frames, filename);

            if (frames.size() == 0)
            {
                cerr << "Couldn't load image " << filename << endl;
                continue;
            }

            if (frames[0].rows() != MATRIX_HEIGHT || frames[0].columns() != MATRIX_WIDTH)
            {
                cerr << "Couldn't load image " << filename << endl;
                continue;
            }

            if (frames.size() > 1)
            {
                std::vector<Magick::Image> result;
                Magick::coalesceImages(&result, frames.begin(), frames.end());

                for (const auto &image : result)
                {
                    int64_t delay_time_us = image.animationDelay() * 10000; // unit in 1/100s
                    storeInStream(image, delay_time_us, offscreen_canvas, &out);
                }
            }
            else
            {
                const Magick::Image &image = frames[0];
                int64_t delay_time_us = IMAGE_DELAY * 1000;
                storeInStream(image, delay_time_us, offscreen_canvas, &out);
            }

            cout << "- " + filename + " (" << to_string(frames.size()) << " frames)" << endl;

            i++;
        }
        catch (std::exception &e)
        {
            cerr << e.what() << endl;
        }
    }

    cout << to_string(i) << " images loaded in " << to_string(GetTimeInMillis() - start_t) << "ms" << endl;
    return stream;
}

#ifdef SHOW_TIME
/**
 * Get the current time as used in `showTime`
 *
 * @param out has 5 elements 0-10
 */
void getTimeToDisplay(int out[]) {
    time_t currentTime;
    time(&currentTime);
    struct tm *localTime = localtime(&currentTime);
    int Hour = localTime->tm_hour;
    int Min = localTime->tm_min;

    out[1] = Hour % 10;
    out[0] = (Hour - out[1]) / 10;
    out[2] = 10; // colon is after the 9 in nums.png file
    out[4] = Min % 10;
    out[3] = (Min - out[4]) / 10;
}

/**
 * Adds the current time to a frame
 * @param timeToDisplay has 5 elements 0-10
 */
void showTime(FrameCanvas *offscreen_canvas, const int timeToDisplay[], const Magick::Image &nums) {
    for (uint8_t i = 0; i < 5; i++)
    {
        for (size_t y = 0; y < nums.rows(); ++y)
        {
            for (size_t x = 0; x < NUMS_WIDTH; ++x)
            {
                copyPixel(nums, offscreen_canvas,
                          timeToDisplay[i] * NUMS_WIDTH + x, y,
                          TIME_X + i * (NUMS_WIDTH + NUMS_SPACE) + x, TIME_Y + y);
            }
        }
    }
}
#endif

int main(int argc, char **argv)
{
    cout << "Start matrix" << endl;

    string local_ip = exec("hostname -I | cut -d' ' -f1");
    cout << "IP is " << local_ip << endl;

    Magick::InitializeMagick(*argv);

    wiringPiSetup();
    Button button(BUTTON_PIN);

    RGBMatrix::Options matrix_options;
    rgb_matrix::RuntimeOptions runtime_opt;

    matrix_options.cols = MATRIX_WIDTH;
    matrix_options.rows = MATRIX_HEIGHT;
    matrix_options.hardware_mapping = "adafruit-hat-pwm";

    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
    if (matrix == NULL)
    {
        cerr << "Couldn't initialize matrix" << endl;
        return 1;
    }

    int fd = inotify_init1(IN_NONBLOCK);
    if (fd == -1)
    {
        cerr << "Couldn't initialize inotify" << endl;
        return 1;
    }

    int wd = inotify_add_watch(fd, IMAGES_FOLDER, IN_MODIFY | IN_CREATE | IN_DELETE);
    if (wd == -1)
    {
        cerr << "Couldn't add watch to " << IMAGES_FOLDER << endl
             << strerror(errno) << endl;
        return 1;
    }

    rgb_matrix::Font font;
    const char *font_file = "4x6.bdf";
    if (!font.LoadFont(font_file))
    {
        cerr << "Couldn't load font" << endl;
        return 1;
    }

    FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();
    FrameCanvas *offscreen_canvas2 = matrix->CreateFrameCanvas();

    rgb_matrix::DrawText(offscreen_canvas, font,
                         2, 2 + font.baseline(),
                         Color(255, 255, 255), NULL,
                         local_ip.c_str(),
                         0);

    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);

    usleep(2000);

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    bool isOn = true;
    button.onSinglePress([&]() {
        isOn = !isOn;
        if (!isOn)
        {
            cout << "OFF" << endl;
            matrix->Clear();
        }
        else
        {
            cout << "ON" << endl;
        }
    });

    #ifdef SHOW_TIME
        int timeToDisplay[5] = {};
        Magick::Image nums = Magick::Image(NUMS_FILE);
    #endif

    bool changes = true;
    vector<string> images_filenames;
    rgb_matrix::StreamIO* stream = NULL;
    rgb_matrix::StreamReader* reader = NULL;

    tmillis_t next = GetTimeInMillis();
    while (!interrupt_received)
    {
        // check FS notifications
        char buffer[INOTIFY_BUF_LEN];
        int length = read(fd, buffer, INOTIFY_BUF_LEN);
        if (length > 0)
        {
            changes = true;
        }

        EVERY_N_MILLIS(50)
        {
            button.handle();
        }

        // reload files if needed
        EVERY_N_MILLIS_I(t, 1)
        {
            t.setPeriod(10000);
            if (changes)
            {
                listImages(images_filenames);
                if (!images_filenames.empty())
                {
                    rgb_matrix::DrawText(offscreen_canvas, font,
                                        2, 2 + font.baseline(),
                                        Color(255, 255, 255), NULL,
                                        "Loading...",
                                        0);
                    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);

                    if (stream != NULL) {
                        delete reader;
                        delete stream;
                    }
                    stream = loadImages(images_filenames, offscreen_canvas2);
                    reader = new rgb_matrix::StreamReader(stream);
                }
                changes = false;
            }
        }

        if (reader == NULL || !isOn)
        {
            continue;
        }

        // display !
        const tmillis_t now = GetTimeInMillis();
        if (now >= next)
        {
            uint32_t delay_us = 0;
            if (reader->GetNext(offscreen_canvas, &delay_us)) {
                const tmillis_t anim_delay_ms = delay_us / 1000;
#ifdef SHOW_TIME
                getTimeToDisplay(timeToDisplay);
                showTime(offscreen_canvas, timeToDisplay, nums);
#endif
                offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);
                next = now + anim_delay_ms;
            } else {
                reader->Rewind();
            }
        }
    }

    matrix->Clear();
    delete matrix;

    inotify_rm_watch(fd, wd);
    close(fd);

    return 0;
}
