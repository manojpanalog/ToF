/********************************************************************************/
/*                                                                              */
/* Copyright (c) 2020 Analog Devices, Inc. All Rights Reserved.                 */
/* This software is proprietary to Analog Devices, Inc. and its licensors.      */
/*                                                                              */
/********************************************************************************/

#include <aditof/camera.h>
#include <aditof/depth_sensor_interface.h>
#include <aditof/frame.h>
#include <aditof/system.h>
#include <aditof/version.h>
#include <chrono>
#include <command_parser.h>
#include <ctime>
#include <fstream>
#ifdef USE_GLOG
#include <glog/logging.h>
#else
#include <aditof/log.h>
#include <cstring>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#endif
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

enum : uint16_t {
    MAX_FILE_PATH_SIZE = 512,
    FRAME_TYPE_LEN = 20,
};

#define MULTI_THREADED 1
#define DATA_COLLECT_VERSION "1.3.0"
#define EMBED_HDR_LENGTH 128

using namespace aditof;

#ifdef _WIN32
int main(int argc, char *argv[]);
#endif

typedef struct thread_params {
    uint16_t *pCaptureData;
    uint8_t *pHeaderData;
    uint16_t nframes;
    uint32_t nFrameNum;
    uint64_t nTotalCaptureSize;
    uint64_t nFrameCount;
    uint64_t pFramesize;
    const char *pFolderPath;
    const char *pFrame_type;
    const char *nFileTime;
} thread_params;

static const char kUsagePublic[] =
    R"(Data Collect.
    Usage:
      data_collect CONFIG
      data_collect [--f <folder>] [--n <ncapture>] [--m <mode>] [--wt <warmup>] [--ccb FILE] [--ip <ip>] [--fw <firmware>] CONFIG
      data_collect (-h | --help)

    Arguments:
      CONFIG            Name of a configuration file (with .json extension)

    Options:
      -h --help          Show this screen.
      --f <folder>       Output folder (max name 512) [default: ./]
      --n <ncapture>     Capture frame num. [default: 1]
      --m <mode>         Mode to capture data in. [default: 0]
      --wt <warmup>      Warmup Time (sec) [default: 0]
      --ccb <FILE>       The path to store CCB content
      --ip <ip>          Camera IP
      --fw <firmware>    Adsd3500 fw file
      --ft <frameType>   FrameType of saved image (raw/depth/ir/conf) [default: depth]

    Note: --m argument supports both index and string (0/sr-native) 

    Valid mode (--m) options are:
        0: short-range native
        1: long-range native
        2: short-range Qnative
        3: long-range Qnative
        4: pcm-native
        5: long-range mixed
        6: short-range mixed
)";

#ifdef MULTI_THREADED
void fileWriterTask(const thread_params *const pThreadParams);
#endif

int main(int argc, char *argv[]) {
    std::map<std::string, struct Argument> command_map = {
        {"-h", {"--help", false, "", ""}},
        {"-f", {"--f", false, "", "."}},
        {"-n", {"--n", false, "", "1"}},
        {"-m", {"--m", false, "", "0"}},
        {"-wt", {"--wt", false, "", "0"}},
        {"-ip", {"--ip", false, "", ""}},
        {"-fw", {"--fw", false, "", ""}},
        {"-fps", {"--fps", false, "", ""}},
        {"-ccb", {"--ccb", false, "", ""}},
        {"-ft", {"--ft", false, "", "depth"}},
        {"-config", {"-CONFIG", true, "last", ""}}};

    CommandParser command;
    std::string arg_error;
    command.parseArguments(argc, argv, command_map);

    int result = command.checkArgumentExist(command_map, arg_error);
    if (result != 0) {
        LOG(ERROR) << "Argument " << arg_error << " doesn't exist! "
                   << "Please check help menu.";
        return -1;
    }

    result = command.helpMenu();
    if (result == 1) {
        LOG(INFO) << kUsagePublic;
        return 0;
    } else if (result == -1) {
        LOG(ERROR) << "Usage of argument -h/--help"
                   << " is incorrect! Help argument should be used alone!";
        return -1;
    }

    result = command.checkValue(command_map, arg_error);
    if (result != 0) {
        LOG(ERROR) << "Argument: " << command_map[arg_error].long_option
                   << " doesn't have assigned or default value!";
        LOG(INFO) << kUsagePublic;
        return -1;
    }

    result = command.checkMandatoryArguments(command_map, arg_error);
    if (result != 0) {
        std::string argName = (arg_error == "-config")
                                  ? "CONFIG"
                                  : command_map[arg_error].long_option;

        LOG(ERROR) << "Mandatory argument: " << argName << " missing";
        LOG(INFO) << kUsagePublic;
        return -1;
    }

    result = command.checkMandatoryPosition(command_map, arg_error);
    if (result != 0) {
        std::string argName = (arg_error == "-config")
                                  ? "CONFIG"
                                  : command_map[arg_error].long_option;

        LOG(ERROR) << "Mandatory argument " << argName
                   << " is not on its correct position ("
                   << command_map[arg_error].position << ").";
        LOG(INFO) << kUsagePublic;
        return -1;
    }

    char folder_path[MAX_FILE_PATH_SIZE]; // Path to store the raw/depth frames
    char json_file_path
        [MAX_FILE_PATH_SIZE]; // Get the .json file from command line
    std::string frame_type; // Type of frame need to be captured (Raw/Depth/IR)

    uint16_t err = 0;
    uint32_t n_frames = 0;
    uint32_t mode = 0;
    uint32_t warmup_time = 0;
    std::string ip;
    std::string firmware;

    google::InitGoogleLogging(argv[0]);
    FLAGS_alsologtostderr = 1;

    LOG(INFO) << "SDK version: " << aditof::getApiVersion()
              << " | branch: " << aditof::getBranchVersion()
              << " | commit: " << aditof::getCommitVersion();

    Status status = Status::OK;
    // Parsing the arguments from command line
    err = snprintf(json_file_path, sizeof(json_file_path), "%s",
                   command_map["-config"].value.c_str());
    if (err < 0) {
        LOG(ERROR) << "Error copying the json file path!";
        return 0;
    }

    // Parsing output folder
    err = snprintf(folder_path, sizeof(folder_path), "%s",
                   command_map["-f"].value.c_str());

    if (err < 0) {
        LOG(ERROR) << "Error copying the output folder path!";
        return 0;
    }
#ifdef _WIN32
    // Create folder if not created already
    char dir_path[MAX_PATH];
    if (GetFullPathName(folder_path, MAX_PATH, &dir_path[0], NULL) == 0) {
        LOG(ERROR) << "Error Unable to get directory. Error:" << GetLastError();
        return 0;
    }

    if (!(CreateDirectory(dir_path, NULL))) {
        if (ERROR_ALREADY_EXISTS != GetLastError()) {
            LOG(ERROR) << "Error creating directory. Error:", GetLastError();
            return 0;
        }
    }

#else
    err = mkdir(folder_path, 0777);

    if (err < 0) {
        LOG(ERROR) << "Unable to create directory";
        return 0;
    }
#endif

    // Parsing number of frames
    n_frames = std::stoi(command_map["-n"].value);

    // Parsing mode type
    std::string modeName;
    try {
        std::size_t counter;
        mode = std::stoi(command_map["-m"].value, &counter);
        if (counter != command_map["-m"].value.size()) {
            throw command_map["-m"].value.c_str();
        }
    } catch (const char *name) {
        modeName = name;
    } catch (const std::exception &) {
        modeName = command_map["-m"].value;
    }

    // Parsing ip
    if (!command_map["-ip"].value.empty()) {
        ip = command_map["-ip"].value;
    }

    // Parsing firmware
    if (!command_map["-fw"].value.empty()) {
        firmware = command_map["-fw"].value;
    }

    frame_type = command_map["-ft"].value;

    if (frame_type.length() <= 0 &&
        (!frame_type.compare(std::string("raw")) ||
         !frame_type.compare(std::string("depth")) ||
         !frame_type.compare(std::string("ir")) ||
         !frame_type.compare(std::string("conf")))) {
        LOG(ERROR) << "Error parsing frame_type (-ft/--ft) from command line!"
                   << "\n Possible values: raw, depth, ir, conf"
                   << "\n Please check help menu";
        return 0;
    }

    //Parsing Warm up time
    if (!command_map["-wt"].value.empty()) {
        warmup_time = std::stoi(command_map["-wt"].value);
        if (warmup_time < 0) {
            LOG(ERROR) << "Invalid warm up time input!";
        }
    }

    //Parsing CCB path
    std::string ccbFilePath;
    if (!command_map["-ccb"].value.empty()) {
        ccbFilePath = command_map["-ccb"].value;
    }

    LOG(INFO) << "Output folder: " << folder_path;
    LOG(INFO) << "Mode: " << command_map["-m"].value;
    LOG(INFO) << "Number of frames: " << n_frames;
    LOG(INFO) << "Json file: " << json_file_path;
    LOG(INFO) << "Frame type is: " << frame_type;
    LOG(INFO) << "Warm Up Time is: " << warmup_time << " seconds";

    if (!ip.empty()) {
        LOG(INFO) << "Ip address is: " << ip;
    }

    if (!firmware.empty()) {
        LOG(INFO) << "Firmware file is is: " << firmware;
    }

    if (!ccbFilePath.empty()) {
        LOG(INFO) << "Path to store CCB content: " << ccbFilePath;
    }

    System system;
    std::vector<std::shared_ptr<Camera>> cameras;

    if (!ip.empty()) {
        ip = "ip:" + ip;
        system.getCameraList(cameras, ip);
    } else {
        system.getCameraList(cameras);
    }

    if (cameras.empty()) {
        LOG(WARNING) << "No cameras found";
        return 0;
    }

    auto camera = cameras.front();

    status = camera->initialize(json_file_path);
    if (status != Status::OK) {
        LOG(ERROR) << "Could not initialize camera!";
        return 0;
    }

    aditof::CameraDetails cameraDetails;
    camera->getDetails(cameraDetails);

    LOG(INFO) << "SD card image version: " << cameraDetails.sdCardImageVersion;
    LOG(INFO) << "Kernel version: " << cameraDetails.kernelVersion;
    LOG(INFO) << "U-Boot version: " << cameraDetails.uBootVersion;

    if (!firmware.empty()) {
        std::ifstream file(firmware);
        if (!(file.good() &&
              file.peek() != std::ifstream::traits_type::eof())) {
            LOG(ERROR) << firmware << " not found or is an empty file";
            return 0;
        }

        status = camera->adsd3500UpdateFirmware(firmware);
        if (status != Status::OK) {
            LOG(ERROR) << "Could not update the adsd3500 firmware";
            return 0;
        } else {
            LOG(INFO) << "Please reboot the board!";
            return 0;
        }
    }

    // Get frame types
    std::vector<std::string> frameTypes;
    status = camera->getAvailableFrameTypes(frameTypes);
    if (status != Status::OK || frameTypes.empty()) {
        LOG(ERROR) << "Could not aquire frame types";
        return 0;
    }

    if (modeName.empty()) {
        status = camera->getFrameTypeNameFromId(mode, modeName);
        if (status != Status::OK) {
            LOG(ERROR) << "Mode: " << mode
                       << " is invalid for this type of camera!";
            return 0;
        }
    }

    std::shared_ptr<DepthSensorInterface> depthSensor = camera->getSensor();
    std::string sensorName;
    status = depthSensor->getName(sensorName);

    // Disable depth compute for raw frames
    if (frame_type == "raw") {
        camera->enableDepthCompute(false);
    }

    // pcm-native contains ir only
    if (frame_type != "ir" && modeName == "pcm-native") {
        LOG(ERROR) << modeName
                   << " mode doesn't contain depth/conf/raw data, setting --ft "
                      "(frameType) to ir.";
        frame_type = "ir";
    }

    status = camera->setFrameType(modeName);
    if (status != Status::OK) {
        LOG(ERROR) << "Could not set camera frame type!";
        return 0;
    }

    char time_buffer[128];
    time_t rawtime;
    time(&rawtime);
    struct tm timeinfo;
#ifdef _WIN32
    localtime_s(&timeinfo, &rawtime);
#else
    localtime_r(&rawtime, &timeinfo);
#endif
    strftime(time_buffer, sizeof(time_buffer), "%Y%m%d%H%M%S", &timeinfo);
#if 0
    camera->setControl("setFPS", std::to_string(setfps));
    if (status != Status::OK) {
        LOG(ERROR) << "Error setting camera FPS to " << setfps;
        return 0;
    }
#endif

    // Store CCB to file
    if (!ccbFilePath.empty()) {
        status = camera->saveModuleCCB(ccbFilePath);
        if (status != Status::OK) {
            LOG(INFO) << "Failed to store CCB to " << ccbFilePath;
        }
    }

    // Program the camera with cfg passed, set the mode by writing to 0x200 and start the camera
    status = camera->start();
    if (status != Status::OK) {
        LOG(ERROR) << "Could not start camera!";
        return 0;
    }

    aditof::Frame frame;
    FrameDetails fDetails;
    std::string frameType;

    uint16_t *frameBuffer;
    uint8_t *headerBuffer;
    uint32_t height;
    uint32_t width;
    uint64_t frame_size = 0;
    uint64_t elapsed_time;

    auto warmup_start = std::chrono::steady_clock::now();

    // Wait until the warmup time is finished
    if (warmup_time > 0) {
        do {
            frameType = "raw";
            uint16_t *pRawFrame;
            status = camera->requestFrame(&frame);
            if (status != Status::OK) {
                LOG(ERROR) << "Could not request frame!";
                return 0;
            }
            status = frame.getData(frameType, &pRawFrame);
            if (status != Status::OK) {
                LOG(ERROR) << "Could not get Raw frame type data!";
                return 0;
            }

            auto warmup_end = std::chrono::steady_clock::now();
            elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(
                               warmup_end - warmup_start)
                               .count();
        } while (warmup_time >= elapsed_time);
    }

    LOG(INFO) << "Requesting " << n_frames << " frames!";
    auto start_time = std::chrono::high_resolution_clock::now();
    // Request the frames for the respective mode
    for (uint32_t loopcount = 0; loopcount < n_frames; loopcount++) {

        status = camera->requestFrame(&frame);
        if (status != Status::OK) {
            LOG(ERROR) << "Could not request frame!";
            return 0;
        }

        frame.getDetails(fDetails);

        height = fDetails.height;
        width = fDetails.width;

        frameType = "raw";
        std::string pixelCount;

        // Depth Data
        if (frame_type == "depth") {
            status = depthSensor->getControl("phaseDepthBits", pixelCount);
            if (!std::stoi(pixelCount)) {
                LOG(ERROR) << "Depth disabled from ini file!";
                return 0;
            }

            frame_size = sizeof(uint16_t) * height * width;
            frameType = "depth";
        }
        // Ir data
        else if (frame_type == "ir") {
            if (modeName != "pcm-native") {
                status = depthSensor->getControl("abBits", pixelCount);
                if (!std::stoi(pixelCount)) {
                    LOG(ERROR) << "IR disabled from ini file!";
                    return 0;
                }
            }

            frame_size = sizeof(uint16_t) * height * width;
            frameType = "ir";
        }
        // Conf data
        else if (frame_type == "conf") {
            status = depthSensor->getControl("confidenceBits", pixelCount);
            if (!std::stoi(pixelCount)) {
                LOG(ERROR) << "Conf disabled from ini file!";
                return 0;
            }

            frame_size = sizeof(float) * height * width;
            frameType = "conf";
        }
        // Raw data
        else if (frame_type == "raw") {
            aditof::FrameDataDetails rawFrameDetails;
            frame.getDataDetails("raw", rawFrameDetails);

            frame_size = rawFrameDetails.width * rawFrameDetails.height *
                         rawFrameDetails.subelementsPerElement *
                         rawFrameDetails.subelementSize;
            frameType = "raw";
        } else {
            LOG(WARNING) << "Can't recognize frame data type!";
        }

        /* Since getData returns the pointer to the depth/raw data, which only main thread has access to,
            we need to copy depth frame to local memory and pass that to thread for file I/O, there is no drop in the throughput with this. */
        frameBuffer = new uint16_t[frame_size];
        if (frameBuffer == NULL) {
            LOG(ERROR) << "Can't allocate Memory for frame type data!";
            return 0;
        }

        uint16_t *pData;
        status = frame.getData(frameType, &pData);
        if (status != Status::OK) {
            LOG(ERROR) << "Could not get frame type data!";
            return 0;
        }
        if (!pData) {
            LOG(ERROR) << "no memory allocated in frame";
            return 0;
        }
        memcpy(frameBuffer, (uint8_t *)pData, frame_size);

        uint32_t header_data_size = EMBED_HDR_LENGTH;
        headerBuffer = new uint8_t[header_data_size];
        if (headerBuffer == NULL) {
            LOG(ERROR) << "Can't allocate Memory for frame header data!";
            return 0;
        }

#if 0 // TO DO: uncomment this one the header becomes available
        uint16_t *pHeader = nullptr;
        status = frame.getData("embeded_header", &pHeader);
        if (status != Status::OK) {
            LOG(ERROR) << "Could not get frame header data!";
            return 0;
        }
        if (!pHeader) {
            LOG(ERROR) << "no memory allocated in frame header";
            return 0;
        }
        memcpy(headerBuffer, (uint8_t *)pHeader, header_data_size);
#endif

        // Create thread to handle the file I/O of copying raw/depth images to file
#ifdef MULTI_THREADED
        thread_params *pThreadParams = new thread_params();
        if (pThreadParams == nullptr) {
            LOG(ERROR) << "Thread param memory allocation failed";
            return 0;
        }
        pThreadParams->pCaptureData = frameBuffer;
        pThreadParams->pHeaderData = headerBuffer;
        pThreadParams->nTotalCaptureSize = frame_size;
        pThreadParams->pFolderPath = folder_path;
        pThreadParams->nFileTime = time_buffer;
        pThreadParams->nframes = n_frames;
        pThreadParams->nFrameCount = loopcount;
        pThreadParams->pFrame_type = frame_type.c_str();
        pThreadParams->pFramesize = height * width;

        /* fileWriterThread handles the copying of raw/depth frames to a file */
        std::thread fileWriterThread(
            fileWriterTask,
            const_cast<const thread_params *const>(pThreadParams));
        if (loopcount == n_frames - 1) {
            // wait for completion on final loop iteration
            fileWriterThread.join();
        } else {
            fileWriterThread.detach();
        }
#else
        char out_file[MAX_FILE_PATH_SIZE];

        snprintf(out_file, sizeof(out_file), "%s/%s_frame_%s_%05u.bin",
                 &folder_path[0], &frame_type[0], time_buffer, loopcount);
        std::ofstream rawFile(out_file, std::ios::out | std::ios::binary |
                                            std::ofstream::trunc);
        rawFile.write((const char *)&frameBuffer[0], frame_size);
        rawFile.close();

        if (frameBuffer != NULL) {
            free((void *)frameBuffer);
        }
        if (headerBuffer != NULL) {
            free((void *)headerBuffer);
        }
#endif
    } // End of for Loop

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> total_time = end_time - start_time;
    if (total_time.count() > 0.0) {
        double measured_fps = (double)n_frames / total_time.count();
        LOG(INFO) << "Measured FPS: " << measured_fps;
    }

    status = camera->stop();
    if (status != Status::OK) {
        LOG(INFO) << "Error stopping camera!";
    }
    return 0;
}

void fileWriterTask(const thread_params *const pThreadParams) {

    if (nullptr == pThreadParams) {
        return;
    }

    char out_file[MAX_FILE_PATH_SIZE] = {0};
    snprintf(out_file, sizeof(out_file), "%s/%s_frame_%s_%05" PRIu64 ".bin",
             pThreadParams->pFolderPath, pThreadParams->pFrame_type,
             pThreadParams->nFileTime, pThreadParams->nFrameCount);
    std::ofstream rawFile(out_file, std::ios::out | std::ios::binary |
                                        std::ofstream::trunc);
    rawFile.write((const char *)pThreadParams->pCaptureData,
                  pThreadParams->nTotalCaptureSize);
    rawFile.close();

    if (pThreadParams->pCaptureData != nullptr) {
        delete[] pThreadParams->pCaptureData;
    }
    if (pThreadParams->pHeaderData != nullptr) {
        delete[] pThreadParams->pHeaderData;
    }
    delete pThreadParams;
}