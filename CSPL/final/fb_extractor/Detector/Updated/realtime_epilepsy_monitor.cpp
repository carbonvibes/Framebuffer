#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cmath>
#include <unistd.h>
#include <pthread.h>

#include <opencv2/opencv.hpp>
#include <opencv2/core/hal/intrin.hpp>

using namespace std;
using namespace std::chrono;
using namespace cv;

// Configuration constants
#define NTHREADS 24
#define PROC_FB_PATH "/proc/drm_fb_raw"
#define PROC_FB_INFO "/proc/drm_fb_pixels"
#define TARGET_FPS 60
#define FRAME_INTERVAL_MS (1000 / TARGET_FPS)

#define pixelDensity(w, h, s) sqrt(w*w + h*h) / s
#define minSafeArea(vd, pd) vd*vd*pd*pd*0.1745*0.1309*0.25

// Frame structure for epilepsy detection
struct Frame {
    Mat I;           // Luminance
    Mat Rr;          // Red ratio
    Mat_<Point2f> S; // Chromaticity coordinates
    int harmfulLumCount, harmfulColCount;
    Mat_<bool> isIncLum, isDecLum;
    Mat_<bool> isIncCol, isDecCol;

    Frame(int resolution_h, int resolution_w) {
        I = Mat(resolution_h, resolution_w, CV_32F);
        Rr = Mat(resolution_h, resolution_w, CV_32F);
        S = Mat_<Point2f>(resolution_h, resolution_w, Point2f(0.0f, 0.0f));
        harmfulLumCount = harmfulColCount = 0;
        isIncLum = Mat_<bool>(resolution_h, resolution_w, false);
        isDecLum = Mat_<bool>(resolution_h, resolution_w, false);
        isIncCol = Mat_<bool>(resolution_h, resolution_w, false);
        isDecCol = Mat_<bool>(resolution_h, resolution_w, false);
    }

    Frame() {}
};

struct ThreadData {
    int index;
    int countLum, countCol;
};

struct FramebufferInfo {
    uint32_t width, height;
    uint32_t format;
    uint32_t pitch;
    bool valid;
};

// Global variables
Mat current_frame;
Frame f[2];
int resolution_h, resolution_w;
bool HDR = false;
vector<float> gammaLUT;
atomic<bool> running(true);
atomic<bool> epilepsy_detected(false);
mutex frame_mutex;
condition_variable frame_ready;

// Fast inverse gamma correction using lookup table
inline float inverseGammaFast(float value) {
    constexpr int LUT_SIZE = 256;
    if (value < 0) return gammaLUT[0];
    else if (value >= LUT_SIZE) return gammaLUT[LUT_SIZE - 1];
    return gammaLUT[(int)value];
}

// Initialize gamma lookup table
void initializeGammaLUT() {
    constexpr int LUT_SIZE = 256;
    gammaLUT.resize(LUT_SIZE);
    
    // Create inverse gamma correction LUT (gamma = 2.2)
    for (int i = 0; i < LUT_SIZE; i++) {
        float normalized = i / 255.0f;
        gammaLUT[i] = pow(normalized, 2.2f);
    }
}

// Read framebuffer information from proc
FramebufferInfo readFramebufferInfo() {
    FramebufferInfo info = {0};
    
    ifstream proc_file(PROC_FB_INFO);
    if (!proc_file.is_open()) {
        cerr << "Cannot open " << PROC_FB_INFO << endl;
        return info;
    }
    
    string line;
    while (getline(proc_file, line)) {
        if (line.find("Dimensions:") != string::npos) {
            sscanf(line.c_str(), "  Dimensions: %ux%u", &info.width, &info.height);
        } else if (line.find("Format:") != string::npos) {
            sscanf(line.c_str(), "  Format: 0x%08x", &info.format);
        } else if (line.find("Pitch:") != string::npos) {
            sscanf(line.c_str(), "  Pitch: %u", &info.pitch);
        }
    }
    
    info.valid = (info.width > 0 && info.height > 0);
    return info;
}

// Convert BGR0 to BGR for OpenCV
Mat convertBGR0toBGR(const uint8_t* bgr0_data, int width, int height) {
    Mat bgr_frame(height, width, CV_8UC3);
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src_idx = (y * width + x) * 4;  // BGR0 = 4 bytes per pixel
            int dst_idx = (y * width + x) * 3;  // BGR = 3 bytes per pixel
            
            // BGR0 format: [B][G][R][0] - copy first 3 bytes, skip padding
            bgr_frame.data[dst_idx + 0] = bgr0_data[src_idx + 0]; // B
            bgr_frame.data[dst_idx + 1] = bgr0_data[src_idx + 1]; // G  
            bgr_frame.data[dst_idx + 2] = bgr0_data[src_idx + 2]; // R
            // Skip bgr0_data[src_idx + 3] - padding byte
        }
    }
    
    return bgr_frame;
}

// Framebuffer capture thread
void framebufferCaptureThread() {
    cout << "Starting framebuffer capture thread..." << endl;
    
    while (running) {
        // Get framebuffer info
        FramebufferInfo info = readFramebufferInfo();
        if (!info.valid) {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }
        
        // Update resolution if changed
        if (resolution_w != info.width || resolution_h != info.height) {
            resolution_w = info.width;
            resolution_h = info.height;
            f[0] = Frame(resolution_h, resolution_w);
            f[1] = Frame(resolution_h, resolution_w);
            cout << "Updated resolution: " << resolution_w << "x" << resolution_h << endl;
        }
        
        // Read raw framebuffer data
        ifstream fb_file(PROC_FB_PATH, ios::binary);
        if (!fb_file.is_open()) {
            cerr << "Cannot open " << PROC_FB_PATH << endl;
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }
        
        // Calculate expected size (4 bytes per pixel for ARGB)
        size_t expected_size = resolution_w * resolution_h * 4;
        vector<uint8_t> fb_data(expected_size);
        
        fb_file.read(reinterpret_cast<char*>(fb_data.data()), expected_size);
        size_t bytes_read = fb_file.gcount();
        fb_file.close();
        
        if (bytes_read < expected_size) {
            cerr << "Incomplete framebuffer read: " << bytes_read << "/" << expected_size << endl;
            this_thread::sleep_for(chrono::milliseconds(FRAME_INTERVAL_MS));
            continue;
        }
        
        // Convert to OpenCV format
        Mat bgr_frame = convertBGR0toBGR(fb_data.data(), resolution_w, resolution_h);
        
        {
            lock_guard<mutex> lock(frame_mutex);
            bgr_frame.convertTo(current_frame, CV_32FC3);
        }
        
        frame_ready.notify_one();
        this_thread::sleep_for(chrono::milliseconds(FRAME_INTERVAL_MS));
    }
}

// Calculate luminance and color of pixels (multi-threaded)
void* calcLumColor(void* arg) {
    ThreadData* threadData = static_cast<ThreadData*>(arg);
    int index = threadData->index;
    int rowsPerThread = ceil(static_cast<double>(resolution_h) / NTHREADS);
    int startRow = index * rowsPerThread;
    int endRow = min(startRow + rowsPerThread, resolution_h);

    for (int i = startRow; i < endRow; i++) {
        Vec3f *pixelRow = current_frame.ptr<Vec3f>(i);
        auto *I_Row = f[1].I.ptr<float>(i);
        auto *Rr_Row = f[1].Rr.ptr<float>(i);
        auto *S_Row = f[1].S.ptr<Point2f>(i);

        for (int j = 0; j < resolution_w; j++) {
            Vec3f &pixel = pixelRow[j];

            // Inverse Gamma Correction
            float r = inverseGammaFast(pixel[2]); // Red channel
            float g = inverseGammaFast(pixel[1]); // Green channel
            float b = inverseGammaFast(pixel[0]); // Blue channel

            // CIE XYZ color space conversion
            float X = b * 0.1804375 + g * 0.3575761 + r * 0.4124564;
            float Y = b * 0.0721750 + g * 0.7151522 + r * 0.2126729;
            float Z = b * 0.9503041 + g * 0.1191920 + r * 0.0193339;

            // Get chromaticity coordinates
            S_Row[j].x = (X+Y+Z==0)? 0 : (4 * X) / (X + 15 * Y + 3 * Z);
            S_Row[j].y = (X+Y+Z==0)? 0 : (9 * Y) / (X + 15 * Y + 3 * Z);

            I_Row[j] = Y; // luminance calculation
            Rr_Row[j] = (r+g+b==0)? 0 : r / (r + g + b); // red ratio calculation
        }
    }

    return nullptr;
}

// Check luminance and color thresholds (multi-threaded)
void* checkLumColThresh(void* arg) {
    ThreadData* threadData = static_cast<ThreadData*>(arg);
    int index = threadData->index;
    int rowsPerThread = ceil(static_cast<double>(resolution_h) / NTHREADS);
    int startRow = index * rowsPerThread;
    int endRow = min(startRow + rowsPerThread, resolution_h);

    int countLum = 0, countCol = 0;

    for (int i = startRow; i < endRow; i++) {
        auto *I1_Row = f[0].I.ptr<float>(i);
        auto *Rr1_Row = f[0].Rr.ptr<float>(i);
        auto *S1_Row = f[0].S.ptr<Point2f>(i);
        auto *I2_Row = f[1].I.ptr<float>(i);
        auto *Rr2_Row = f[1].Rr.ptr<float>(i);
        auto *S2_Row = f[1].S.ptr<Point2f>(i);

        auto *isIncLum1_Row = f[0].isIncLum.ptr<bool>(i);
        auto *isIncCol1_Row = f[0].isIncCol.ptr<bool>(i);
        auto *isDecLum1_Row = f[0].isDecLum.ptr<bool>(i);
        auto *isDecCol1_Row = f[0].isDecCol.ptr<bool>(i);
        auto *isIncLum2_Row = f[1].isIncLum.ptr<bool>(i);
        auto *isIncCol2_Row = f[1].isIncCol.ptr<bool>(i);
        auto *isDecLum2_Row = f[1].isDecLum.ptr<bool>(i);
        auto *isDecCol2_Row = f[1].isDecCol.ptr<bool>(i);

        for (int j = 0; j < resolution_w; j++) {
            float dx = S1_Row[j].x - S2_Row[j].x;
            float dy = S1_Row[j].y - S2_Row[j].y;

            bool isHarmfulLum = (abs(I1_Row[j]-I2_Row[j]) > 0.1) && (I1_Row[j] < 0.8 || I2_Row[j] < 0.8);
            bool isHarmfulCol = (Rr1_Row[j] >= 0.8 || Rr2_Row[j] >= 0.8) && ((dx*dx + dy*dy) > 0.04);

            // Michaelson contrast for HDR
            if (HDR) {
                isHarmfulLum = isHarmfulLum || ((abs(I1_Row[j]-I2_Row[j])/(I1_Row[j]+I2_Row[j]) > 0.05882352941) && (I1_Row[j] > 0.8 && I2_Row[j] > 0.8));
            }

            if (isHarmfulLum) {
                isIncLum2_Row[j] = (I1_Row[j] < I2_Row[j]);
                isDecLum2_Row[j] = (I1_Row[j] > I2_Row[j]);
                if ((isIncLum1_Row[j] && isDecLum2_Row[j]) || (isDecLum1_Row[j] && isIncLum2_Row[j])) {
                    countLum += 1;
                    isDecLum2_Row[j] = false;
                    isIncLum2_Row[j] = false;
                }
            } else {
                isIncLum2_Row[j] = isIncLum1_Row[j];
                isDecLum2_Row[j] = isDecLum1_Row[j];
            }

            if (isHarmfulCol) {
                isIncCol2_Row[j] = (Rr1_Row[j] < Rr2_Row[j]);
                isDecCol2_Row[j] = (Rr1_Row[j] > Rr2_Row[j]);
                if ((isIncCol1_Row[j] && isDecCol2_Row[j]) || (isDecCol1_Row[j] && isIncCol2_Row[j])) {
                    countCol += 1;
                    isDecCol2_Row[j] = false;
                    isIncCol2_Row[j] = false;
                }
            } else {
                isIncCol2_Row[j] = isIncCol1_Row[j];
                isDecCol2_Row[j] = isDecCol1_Row[j];
            }
        }
    }

    threadData->countLum = countLum;
    threadData->countCol = countCol;

    return nullptr;
}

// Main epilepsy detection thread
void epilepsyDetectionThread() {
    cout << "Starting epilepsy detection thread..." << endl;
    
    // Initialize parameters (can be made configurable)
    int screenSize = 24;      // inches
    int viewingDistance = 60; // cm
    
    initializeGammaLUT();
    
    int freqLum = 0, freqCol = 0;
    queue<Frame> oneSecFrames;
    bool hasFlash = false, hasRed = false;
    
    pthread_t threads[NTHREADS];
    ThreadData threadData[NTHREADS];
    
    auto last_frame_time = high_resolution_clock::now();
    
    while (running) {
        unique_lock<mutex> lock(frame_mutex);
        frame_ready.wait(lock, []{return !current_frame.empty() || !running;});
        
        if (!running) break;
        if (current_frame.empty()) continue;
        
        // Check if we have valid resolution
        if (resolution_w == 0 || resolution_h == 0) {
            lock.unlock();
            this_thread::sleep_for(chrono::milliseconds(10));
            continue;
        }
        
        // Calculate minimum safe area
        double pixelDensity = (screenSize == -1) ? 0 : pixelDensity(resolution_w, resolution_h, screenSize);
        int minSafeArea = (viewingDistance == -1) ? 0.25 * resolution_h * resolution_w : minSafeArea(viewingDistance, pixelDensity);
        
        lock.unlock();
        
        // Multi-threaded luminance and color calculation
        for (int i = 0; i < NTHREADS; i++) {
            threadData[i].index = i;
            if (pthread_create(&threads[i], nullptr, &calcLumColor, &threadData[i]) != 0) {
                perror("Failed to create calcLumColor thread");
            }
        }
        
        for (int i = 0; i < NTHREADS; i++) {
            if (pthread_join(threads[i], nullptr) != 0) {
                perror("Failed to join calcLumColor thread");
            }
        }

        // Multi-threaded harmful luminance and color differences check
        for (int i = 0; i < NTHREADS; i++) {
            threadData[i].index = i;
            threadData[i].countLum = threadData[i].countCol = 0;
            if (pthread_create(&threads[i], nullptr, &checkLumColThresh, &threadData[i]) != 0) {
                perror("Failed to create checkLumColThresh thread");
            }
        }

        for (int i = 0; i < NTHREADS; i++) {
            if (pthread_join(threads[i], nullptr) != 0) {
                perror("Failed to join checkLumColThresh thread");
            }
        }

        // Aggregate results
        for (int i = 0; i < NTHREADS; i++) {
            f[1].harmfulLumCount += threadData[i].countLum;
            f[1].harmfulColCount += threadData[i].countCol;
        }

        // Check area threshold && Update total number of harmful flashes
        if (f[1].harmfulLumCount > minSafeArea) freqLum++;
        if (f[1].harmfulColCount > minSafeArea) freqCol++;

        // Sliding window keeps only one-second worth of frames
        oneSecFrames.push(f[1]);

        if (oneSecFrames.size() > TARGET_FPS) {
            if (oneSecFrames.front().harmfulLumCount > minSafeArea) freqLum--;
            if (oneSecFrames.front().harmfulColCount > minSafeArea) freqCol--;
            oneSecFrames.pop();
        }

        // Check frequency threshold
        if (freqLum > 3) hasFlash = true;
        if (freqCol > 3) hasRed = true;

        if (hasFlash || hasRed) {
            epilepsy_detected = true;
            cout << "\n*** EPILEPSY RISK DETECTED! ***" << endl;
            cout << "Flash detected: " << hasFlash << endl;
            cout << "Red pattern detected: " << hasRed << endl;
            cout << "Harmful luminance count: " << f[1].harmfulLumCount << endl;
            cout << "Harmful color count: " << f[1].harmfulColCount << endl;
            cout << "********************************\n" << endl;
            
            // Reset for next detection window
            hasFlash = hasRed = false;
            freqLum = freqCol = 0;
            while (!oneSecFrames.empty()) oneSecFrames.pop();
        }

        // Shift frames by 1
        swap(f[0], f[1]);
        f[1] = Frame(resolution_h, resolution_w);
    }
}

int main(int argc, char* argv[]) {
    cout << "Real-time Framebuffer Epilepsy Monitor" << endl;
    cout << "======================================" << endl;
    
    // Check if kernel module is loaded
    ifstream test_file(PROC_FB_INFO);
    if (!test_file.is_open()) {
        cerr << "Error: Cannot access " << PROC_FB_INFO << endl;
        cerr << "Make sure the DRM framebuffer extractor kernel module is loaded." << endl;
        return 1;
    }
    test_file.close();
    
    // Parse command line arguments
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (string(argv[i]) == "-HDR") {
                HDR = true;
                cout << "HDR mode enabled" << endl;
            }
        }
    }
    
    cout << "Starting monitoring threads..." << endl;
    
    // Start capture and detection threads
    thread capture_thread(framebufferCaptureThread);
    thread detection_thread(epilepsyDetectionThread);
    
    cout << "Monitoring active. Press Enter to stop..." << endl;
    cin.get();
    
    cout << "Stopping monitor..." << endl;
    running = false;
    frame_ready.notify_all();
    
    capture_thread.join();
    detection_thread.join();
    
    cout << "Monitor stopped." << endl;
    return 0;
}
