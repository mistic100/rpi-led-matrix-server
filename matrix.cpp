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
#include "led-matrix.h"
#include "content-streamer.h"
#include "graphics.h"
#include "lib8tion.h"

#define INOTIFY_BUF_LEN sizeof(struct inotify_event) + NAME_MAX + 1

using namespace std;
using namespace rgb_matrix;

typedef int64_t tmillis_t;

const char *IMAGES_FOLDER = "./images/";
const string IMAGE_EXTENSION_PNG = ".png";
const string IMAGE_EXTENSION_GIF = ".gif";
const uint16_t IMAGE_DELAY = 5000;
const uint8_t MATRIX_WIDTH = 64;
const uint8_t MATRIX_HEIGHT = 32;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo)
{
    interrupt_received = true;
}

/**
 * Get current time in milliseconds
 */
static tmillis_t GetTimeInMillis() {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

/**
 * Sleep for X milliseconds
 */
static void SleepMillis(tmillis_t milli_seconds) {
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
            const Magick::Color &c = img.pixelColor(x, y);
            if (c.alphaQuantum() < 256) 
            {
                scratch->SetPixel(x, y,
                                  ScaleQuantumToChar(c.redQuantum()),
                                  ScaleQuantumToChar(c.greenQuantum()),
                                  ScaleQuantumToChar(c.blueQuantum()));
            }
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

            if (frames[0].rows() != MATRIX_WIDTH || frames[0].columns() != MATRIX_HEIGHT)
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


int main(int argc, char **argv)
{
    cout << "Start matrix" << endl;

    string local_ip = exec("hostname -I | cut -d' ' -f1");
    cout << "IP is " << local_ip << endl;

    Magick::InitializeMagick(*argv);

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
    const char *font_file = "5x8.bdf";
    if (!font.LoadFont(font_file))
    {
        cerr << "Couldn't load font" << endl;
        return 1;
    }

    FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();

    rgb_matrix::DrawText(offscreen_canvas, font,
                         2, 2 + font.baseline(),
                         Color(255, 255, 255), NULL,
                         local_ip.substr(0, 7).c_str(),
                         0);
    rgb_matrix::DrawText(offscreen_canvas, font,
                         2, 2 + font.baseline() * 2,
                         Color(255, 255, 255), NULL,
                         local_ip.substr(7).c_str(),
                         0);

    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);

    usleep(1000);

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    bool changes = true;
    vector<string> images_filenames;
    rgb_matrix::StreamIO* stream = NULL;
    rgb_matrix::StreamReader* reader = NULL;

    while (!interrupt_received)
    {
        char buffer[INOTIFY_BUF_LEN];
        int length = read(fd, buffer, INOTIFY_BUF_LEN);
        if (length > 0)
        {
            changes = true;
        }

        EVERY_N_MILLIS(10000)
        {
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
                    stream = loadImages(images_filenames, offscreen_canvas);
                    reader = new rgb_matrix::StreamReader(stream);
                    changes = false;
                }
            }
        }

        uint32_t delay_us = 0;
        if (reader != NULL) {
            if (reader->GetNext(offscreen_canvas, &delay_us)) {
                const tmillis_t anim_delay_ms = delay_us / 1000;
                const tmillis_t start_wait_ms = GetTimeInMillis();
                offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);
                const tmillis_t time_already_spent = GetTimeInMillis() - start_wait_ms;
                SleepMillis(anim_delay_ms - time_already_spent);
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
