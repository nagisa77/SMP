# Streaming Media Platform (SMP)

## About && Features 

The SMP Streaming Media Platform is a streaming media platform. It supports streaming or pulling streams by stream name and currently supports three protocols: StreamInfo, CodecInfo, and Packet, which manage the initiation of streaming or pulling, codec information, and audio/video frames, respectively.

On the network side, it utilizes asynchronous IO based on boost::Asio, allowing for concurrent handling of a large number of streaming and pulling requests.

## Installation

1. `git clone https://github.com/nagisa77/SMP`  
   Clone the repository to your local machine.

2. `mkdir build`  
   Create a new directory named 'build' for an out-of-source build.

3. `cd build`  
   Change to the newly created 'build' directory.

4. `cmake ..`  
   Run CMake to configure the project and generate build files.

5. `make`  
   Compile the project using the generated makefiles.

6. `./SMP`  
   Run the SMP executable.


### Prerequisites

### Prerequisites

Before installing SMP, ensure that you have the following prerequisites installed:

1. **CMake 3.10 or higher**: SMP requires CMake version 3.10 or newer for configuration and generating build files.

2. **C++ Compiler with C++14 support**: The project is set to use C++14 standard.

3. **spdlog**: This logging library is required for the project.

4. **nlohmann_json**: A JSON library for modern C++.

5. **Boost Libraries (system and thread)**: Boost libraries are used for system and thread handling.

6. **Qt6 (Core and Widgets)**: Qt6 components are required for GUI elements and core functionalities.

7. **FFmpeg Libraries**: 
   - `libavformat` (AVFORMAT_LIBRARY)
   - `libavcodec` (AVCODEC_LIBRARY)
   - `libavutil` (AVUTIL_LIBRARY)
   - `libswscale` (SWSCALE_LIBRARY)
   - `libswresample` (SWRESAMPLE_LIBRARY)
   
   These libraries are used for media encoding/decoding and related functionalities.

Make sure these dependencies are correctly installed and configured in your system before proceeding with the SMP installation.

## Usage

The SMP platform operates using three distinct protocols, each managed via JSON messaging. These protocols are vital for the efficient and flexible handling of streaming data. Below is an expanded explanation of each protocol and its role in the streaming process.

### 1. StreamInfo (kTypeStreamInfo)

`StreamInfo` is pivotal for initiating and managing the streaming process, be it pushing or pulling streams.

- **Example Usage:**
  ```cpp
  JSON start_push;
  start_push["type"] = MessageType::kTypeStreamInfo;
  start_push["is_push"] = true; // Set to false for pulling
  start_push["stream_id"] = "stream-identifier";
  start_push["enable"] = true;
  send_json(socket, start_push);
  ```
  - `is_push`: Determines the stream direction (push or pull).
  - `stream_id`: A unique identifier for the stream.
  - `enable`: Controls the start/stop status of the stream.

This protocol is the foundation of stream management, enabling precise control over stream initiation and termination.

### 2. CodecInfo (kTypeCodecInfo)

The `CodecInfo` protocol handles the transmission of codec configuration data, essential for setting up encoders and decoders.

- **Example Usage:**
  ```cpp
  JSON codec_info;
  codec_info["type"] = MessageType::kTypeCodecInfo;
  codec_info["codec_id"] = pCodecCtx->codec_id;
  codec_info["width"] = pCodecCtx->width;
  codec_info["height"] = pCodecCtx->height;
  codec_info["pix_fmt"] = pCodecCtx->pix_fmt;
  codec_info["extradata_size"] = pCodecCtx->extradata_size;
  codec_info["extradata"] = base64_encode(pCodecCtx->extradata, pCodecCtx->extradata_size);
  send_json(socket, codec_info);
  ```
  - `codec_id`: Specifies the codec.
  - `width`, `height`: Video dimensions.
  - `pix_fmt`: Pixel format.
  - `extradata_size`, `extradata`: Additional codec data.

This step ensures that both the sender and receiver are synchronized in terms of how the media data should be encoded/decoded, a cornerstone for high-quality streaming.

### 3. Packet (kTypePacket)

`Packet` is the protocol for the actual transfer of audio/video data packets.

- **Example Usage:**
  ```cpp
  JSON packet_info;
  packet_info["type"] = MessageType::kTypePacket;
  packet_info["packet_size"] = *packet_size;
  packet_info["pts"] = pkt->pts;
  packet_info["dts"] = pkt->dts;
  packet_info["stream_index"] = pkt->stream_index;
  packet_info["duration"] = pkt->duration;
  packet_info["pos"] = pkt->pos;
  send_json(socket, packet_info);
  // Additional code for packet data handling...
  ```
  - `packet_size`: Size of the packet.
  - `pts`, `dts`: Presentation and decoding timestamps.
  - `stream_index`: Stream identifier.
  - `duration`: Packet duration.
  - `pos`: Position in the stream.

Following the JSON packet information, the `packet_size` length of binary data is sent and parsed as an `AVPacket`. This approach ensures that the packet data is tightly coupled with its metadata, facilitating accurate and efficient data processing.

### Stream Data Handling Sequence

- **Pusher Sequence**: The pusher follows a sequence of sending `StreamInfo` first, then `CodecInfo`, and finally the `Packet` data. This orderly progression ensures that the receiver is fully prepared and synchronized before the actual media data is transmitted.
- **Puller Sequence**: Conversely, the puller receives data in the reverse order. It first gets the packet data, then the codec information, and finally the stream information. This sequence allows the puller to immediately process the media data as it is received.

### The Elegance of SMP's Design

The design of SMP's protocols reflects a deep understanding of the nuances of streaming media. By segregating data handling into distinct protocols, SMP provides a clear and modular approach to streaming, making it easier to manage and troubleshoot. This separation also enhances the scalability of the platform, as each component can be independently optimized or replaced as needed. Furthermore, the use of JSON for messaging not only simplifies the data exchange process but also adds a layer of readability and flexibility, accommodating future enhancements with minimal disruption.
