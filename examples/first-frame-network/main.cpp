/*
 * BSD 3-Clause License
 *
 * Copyright (c) 2019, Analog Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <ctime>
#include <string>
#include <iomanip>
#include <aditof/camera.h>
#include <aditof/frame.h>
#include <aditof/system.h>
#include <aditof/version.h>
#ifdef USE_GLOG
#include <glog/logging.h>
#else
#include <aditof/log.h>
#endif

using namespace aditof;

#define SAVE_DEPTH_FRAME
#define SAVE_IR_FRAME
#define SAVE_RAW_FRAME

static std::string current_local_time_string() {
    std::time_t raw_time;
    std::time(&raw_time);
    std::tm *local_time = std::localtime(&raw_time);

    char buffer[16];
    strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", local_time);
    return std::string(buffer);
}

static double get_seconds() {
    LARGE_INTEGER time, frequency;
    QueryPerformanceCounter(&time);
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(time.QuadPart) /
           static_cast<double>(frequency.QuadPart);
}

static Status save_frame(aditof::Frame &frame, std::string frameType, std::string nameappend) {

    uint16_t *data1;
    FrameDataDetails fDetails;
    Status status = Status::OK;

    status = frame.getData(frameType, &data1);
    if (status != Status::OK) {
        LOG(ERROR) << "Could not get frame data " + frameType + "!";
        return status;
    }

    if (!data1) {
        LOG(ERROR) << "no memory allocated in frame";
        return status;
    }

    std::ofstream g("out_" + frameType + "_" + nameappend + ".bin",
                    std::ios::binary);
    frame.getDataDetails(frameType, fDetails);
    g.write((char *)data1, fDetails.width * fDetails.height * sizeof(uint16_t));
    g.close();

    return status;
}

int main(int argc, char *argv[]) {

    google::InitGoogleLogging(argv[0]);
    FLAGS_alsologtostderr = 1;

    LOG(INFO) << "SDK version: " << aditof::getApiVersion()
              << " | branch: " << aditof::getBranchVersion()
              << " | commit: " << aditof::getCommitVersion();

    Status status = Status::OK;

    if (argc != 5) {
        LOG(ERROR) << "No ip or config file provided! " << argv[0] << " <mode> <ip> <JSON config_file>";
        return -1;
    }

    std::string mode = argv[1];
    uint32_t FRAMES = std::stoi(argv[2]);
    std::string ip = argv[3];
    std::string configFile = argv[4];
    System system;

    if (!(mode == "mp" || mode == "qmp")) {
        LOG(INFO) << "qmp - quarter megapixel mode.";
        LOG(INFO) << "mp - megapixel mode.";
        return -2;
    }

    std::vector<std::shared_ptr<Camera>> cameras;
    system.getCameraListAtIp(cameras, ip);
    if (cameras.empty()) {
        LOG(WARNING) << "No cameras found";
        return -3;
    }

    auto camera = cameras.front();

    status = camera->setControl("initialization_config", configFile);
    if(status != Status::OK){
        LOG(ERROR) << "Failed to set control!";
        return -4;
    }

    status = camera->initialize();
    if (status != Status::OK) {
        LOG(ERROR) << "Could not initialize camera!";
        return -5;
    }

    aditof::CameraDetails cameraDetails;
	camera->getDetails(cameraDetails);

	LOG(INFO) << "SD card image version: " << cameraDetails.sdCardImageVersion;
	LOG(INFO) << "Kernel version: " << cameraDetails.kernelVersion;
	LOG(INFO) << "U-Boot version: " << cameraDetails.uBootVersion;

    std::vector<std::string> frameTypes;
    camera->getAvailableFrameTypes(frameTypes);
    if (frameTypes.empty()) {
        std::cout << "no frame type available!";
        return -6;
    }
    status = camera->setFrameType(mode);
    if (status != Status::OK) {
        LOG(ERROR) << "Could not set camera frame type!";
        return -7;
    }

    status = camera->start();
    if (status != Status::OK) {
        LOG(ERROR) << "Could not start the camera!";
        return -8;
    }

    uint32_t framesize;

    if (mode == "mp") {
        framesize = (1024 * 1024) * (3 * 12 + 16) / 8; // 3x 12-bit phases + 1x 16-bit AB frames
    } else if (mode == "qmp") {
        framesize = (512 * 512) * (16 + 16 + 8) / 8; // 16-bit radial + 16-bit AB + 8-bit Confidence frames
    } else {
        LOG(ERROR) << "Unable to determine the frame size";
        return -9;
    }

    LOG(INFO) << "'" << mode << "' expected frame size: " << framesize;

    LOG(INFO) << "Getting " << FRAMES << " frames";

    double st = get_seconds();
    for (uint32_t cnt = 0; cnt < FRAMES;) {
        aditof::Frame frame;
        camera->requestStreamFrame(&frame);


        cnt++;
        LOG(INFO) << "Frame #: " << cnt << ", " << framesize << " bytes";

        std::stringstream ss;
        ss << std::setfill('0') << std::setw(5) << cnt;
        std::string formatted_value = ss.str();

#ifdef SAVE_IR_FRAME
        save_frame(frame, "ir",
                   current_local_time_string() + "_" + formatted_value);
#endif
#ifdef SAVE_DEPTH_FRAME
        save_frame(frame, "depth",
                   current_local_time_string() + "_" + formatted_value);
#endif
#ifdef SAVE_RAW_FRAME
        uint16_t *pData;
        status = frame.getData("raw", &pData);

        std::string filename = "raw_frame_" + current_local_time_string() +
                        "_" + formatted_value + ".bin";

        std::ofstream file(std::string(filename), std::ios::binary);
        if (file.is_open()) {
            file.write((const char *)pData, framesize);
            file.close();
        } else {
            std::cerr << "Error opening file for writing" << std::endl;
        }
#endif
    }
    double et = get_seconds();

    status = camera->stop();
    if (status != Status::OK) {
        LOG(ERROR) << "Could not start the camera!";
        return -10;
    }

    LOG(INFO) << "Frame rate = " << (static_cast<double>(FRAMES) / (et - st)) << "s";

    return 0;
}