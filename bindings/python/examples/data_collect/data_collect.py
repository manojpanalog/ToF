#
# BSD 3-Clause License
#
# Copyright (c) 2019, Analog Devices, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
import aditofpython as tof
import argparse

mode_help_message = """Valid mode (-m) options are:
        0: short-range native;
        1: long-range native;
        2: short-range Qnative;
        3: long-range Qnative
        4: pcm-native;
        5: long-range mixed;
        6: short-range mixed"""


IP = '10.42.0.1'
FRAME_TYPES = ['raw', 'depth', 'ir', 'conf']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Script to run data collect executible')
    parser.add_argument(
        'config', help='path to the configuration file (with .json extension)')
    parser.add_argument('-f', dest='folder', default='./',
                        help='output folder')
    parser.add_argument('-n', dest='ncapture', type=int, default=1,
                        help='number of frame captured')
    parser.add_argument('-m', dest='mode', type=int, default=0,
                        help=mode_help_message)
    parser.add_argument('-wt', dest='warmup_time', type=int,
                        default=0, help='warmup time in seconds')
    parser.add_argument('--ip', default=IP, help='camera IP')
    parser.add_argument('-fw', dest='firmware', help='Adsd3500 firmware file')
    parser.add_argument('-ft', dest='frame_type', choices=FRAME_TYPES,
                        default='depth', help='FrameType of saved image')

    args = parser.parse_args()

    try:
        system = tof.System()
    except:
        print("Failed to create system")

    cameras = []
    status = system.getCameraList(cameras, "ip:" + args.ip)
    if not status:
        print("system.getCameraList(): ", status)

    camera1 = cameras[0]
    status = camera1.initialize(args.config)
    if not status:
        print('camera1.initialize()', status)

    status = camera1.initialize()
    if not status:
        print('camera1.initialize()', status)

    cam_details = tof.CameraDetails()
    status = camera1.getDetails(cam_details)
    if not status:
        print('camera1.getDetails()', status)
    print(f'Camera ID: {cam_details.cameraId}')

    if (args.firmware):
        status = camera1.adsd3500UpdateFirmware(args.firmware)
        if not status:
            print('Could not update the Adsd3500 firmware')
        else:
            print('Please reboot the board')

    frame_types = []
    status = camera1.getAvailableFrameTypes(frame_types)
    if not status or len(frame_types) == 0:
        print('Cound not aquire frame types')
    else:
        print(f'available frame_types: {frame_types}')

    mode_names = []
    if len(mode_names) == 0:
        status = camera1.getFrameTypeNameFromId(args.mode, mode_names)
        print(f'mode_id: {args.mode}, status: {status}')
        if not status:
            print(f'Mode: {args.mode} is invalid for this type of camera')
    print(f'mode_names: {mode_names}')

    if len(mode_names) > 0:
        mode_name = mode_names[-1]
    frame_type = args.frame_type
    if frame_type == 'raw':
        camera1.enableDepthCompute(False)
    if frame_type != 'ir' and mode_name == 'pcm-native':
        print(
            f'{mode_name} mode doesn\'t contain depth/conf/raw data, setting --ft (frameType) to ir.')
        frame_type = 'ir'

    status = camera1.setFrameType(mode_name)
    if not status:
        print('Could not set camera frame type')

    status = camera1.start()
    if not status:
        print('Could not start camera')

    print('Done')
