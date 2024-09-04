#include "buffer_utils.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <tl/expected.hpp>


#define ENABLE_STREAMING    0   //Enable streaming, set to 0 to disable streaming and just save the encoded to file
#define USE_RTSP_STREAMING  1   //Enable RTSP streaming, set to 0 to use UDP streaming

#define FRONTEND_CONFIG_FILE "frontend_config_example.json"
#define ENCODER_OSD_CONFIG_FILE(id) get_encoder_osd_config_file(id)
#define OUTPUT_FILE(id) get_output_file(id)

#if ENABLE_STREAMING    
#include "streaming/H15Streaming.hpp"
#endif


struct MediaLibrary
{
    MediaLibraryFrontendPtr frontend;
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> encoders;
    std::map<output_stream_id_t, std::ofstream> output_files;
    
    std::map<output_stream_id_t, std::string> json_encoder_param;
#if ENABLE_STREAMING    
    std::map<output_stream_id_t, H15StreamingPtr> streamings;
#endif    
};

inline std::string get_encoder_osd_config_file(const std::string &id)
{
    return "frontend_encoder_" + id + ".json";
}

inline std::string get_output_file(const std::string &id)
{
    return "frontend_example_" + id + ".h264";
}

#if ENABLE_STREAMING    
void stream_encoded_data(HailoMediaLibraryBufferPtr buffer, uint32_t size, H15StreamingPtr streaming, std::string &json_encode_param)
{
    char *data = (char *)buffer->get_plane(0);
    if (!data)
    {
        std::cout << "Error occurred at streaming time!" << std::endl;
        return;
    }
    
    if (streaming)
        streaming->feedData(data, size);
}
#endif

void write_encoded_data(HailoMediaLibraryBufferPtr buffer, uint32_t size, std::ofstream &output_file)
{
    char *data = (char *)buffer->get_plane(0);
    if (!data)
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    output_file.write(data, size);
}

std::string read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error("config path is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)),
                            std::istreambuf_iterator<char>());
    file_to_read.close();
    std::cout << "Read config from file: " << file_path << std::endl;
    return file_string;
}

void delete_output_file(std::string output_file)
{
    std::ofstream fp(output_file.c_str(), std::ios::out | std::ios::binary);
    if (!fp.good())
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    fp.close();
}

void subscribe_elements(std::shared_ptr<MediaLibrary> media_lib)
{
    auto streams = media_lib->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    FrontendCallbacksMap fe_callbacks;
    for (auto s : streams.value())
    {
        fe_callbacks[s.id] = [s, media_lib](HailoMediaLibraryBufferPtr buffer, size_t size)
        {
            media_lib->encoders[s.id]->add_buffer(buffer);
        };
    }
    media_lib->frontend->subscribe(fe_callbacks);

    for (const auto &entry : media_lib->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "subscribing to encoder for '" << streamId << "'" << std::endl;
        media_lib->encoders[streamId]->subscribe(
            [media_lib, streamId](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
#if ENABLE_STREAMING                
                stream_encoded_data(buffer, size, media_lib->streamings[streamId], media_lib->json_encoder_param[streamId]);
#else
                //write_encoded_data(buffer, size, media_lib->output_files[streamId]);
#endif                
            });
    }
}


#include "media_library/signal_utils.hpp"
std::shared_ptr<MediaLibrary> media_lib;

int main(int argc, char *argv[])
{
    media_lib = std::make_shared<MediaLibrary>();

    // register signal SIGINT and signal handler  
    signal_utils::register_signal_handler([](int signal)
                                        {
                                        std::cout << "Stopping Pipeline..." << std::endl;
                                        media_lib->frontend->stop();
                                        for (const auto &entry : media_lib->encoders)
                                        {
                                            entry.second->stop();
                                        }

                                        // close all file in m_media_lib->output_files
                                        for (auto &entry : media_lib->output_files)
                                        {
                                            entry.second.close();
                                        }

                                        // terminate program  
                                        exit(signal);; });

    // Create and configure frontend
    std::string preproc_config_string = read_string_from_file(FRONTEND_CONFIG_FILE);
    tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected = MediaLibraryFrontend::create(FRONTEND_SRC_ELEMENT_V4L2SRC, preproc_config_string);
    if (!frontend_expected.has_value())
    {
        std::cout << "Failed to create frontend" << std::endl;
        return 1;
    }
    media_lib->frontend = frontend_expected.value();

    auto streams = media_lib->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    for (auto s : streams.value())
    {
        // Create and configure encoder
        std::cout << "Creating encoder enc_" << s.id << std::endl;
        std::string encoderosd_config_string = read_string_from_file(ENCODER_OSD_CONFIG_FILE(s.id).c_str());
        tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected = MediaLibraryEncoder::create(encoderosd_config_string, s.id);
        if (!encoder_expected.has_value())
        {
            std::cout << "Failed to create encoder osd" << std::endl;
            return 1;
        }
        media_lib->encoders[s.id] = encoder_expected.value();

        // create and configure output file
        std::string output_file_path = OUTPUT_FILE(s.id);
        delete_output_file(output_file_path);
        media_lib->output_files[s.id].open(output_file_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
        if (!media_lib->output_files[s.id].good())
        {
            std::cout << "Error occurred at writing time!" << std::endl;
            return 1;
        }

        // Add Json encoder param
        media_lib->json_encoder_param[s.id] = encoderosd_config_string;

#if ENABLE_STREAMING

        // For each RTSP stream we need to assign different port number
        static int portNum = 5000;

        media_lib->streamings[s.id] = std::make_shared<H15Streaming>();
        H15StreamConfig streamConfig;
#if USE_RTSP_STREAMING        
        streamConfig.streamOutSource = SOURCE_OUT_RTSP;
        streamConfig.streamOutRTSPPath = "/H15RtspStream_" + s.id;
        streamConfig.streamOutIP = "10.0.0.1";
#else
        streamConfig.streamOutSource = SOURCE_OUT_UDP;
        streamConfig.streamOutIP = "10.0.0.2";
#endif
        streamConfig.streamOutPort = portNum;
        streamConfig.encodingConfig = media_lib->streamings[s.id]->GetEncodingInfo(media_lib->json_encoder_param[s.id]);
        media_lib->streamings[s.id]->Config(streamConfig);
        media_lib->streamings[s.id]->start();
        portNum += 2;  //Increase the port Number for next stream
#endif

    }

    subscribe_elements(media_lib);

    std::cout << "Starting frontend." << std::endl;
    for (const auto &entry : media_lib->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "starting encoder for " << streamId << std::endl;
        encoder->start();
    }
    media_lib->frontend->start();

    std::cout << "Started playing for 1 hours." << std::endl;

    for (int i = 1; i < (60*24); i++) {
        std::this_thread::sleep_for(std::chrono::seconds(60)); // sleep for X seconds    
        std::cout << "Total Running Time in Minutes is " << i << std::endl;

        for (const auto &entry : media_lib->encoders)
        {
            output_stream_id_t streamId = entry.first;
            MediaLibraryEncoderPtr encoder = entry.second;
            std::cout << "Encoder Current fps for " << streamId << " is " << encoder->get_current_fps() << std::endl;
        }

        media_lib->frontend->get_current_fps();
        std::cout << "Frontend Current fps is " << media_lib->frontend->get_current_fps() << std::endl;

    }

    std::cout << "Stopping." << std::endl;
    media_lib->frontend->stop();

    for (const auto &entry : media_lib->encoders)
    {
        entry.second->stop();
    }

#if ENABLE_STREAMING
    //Release all the streaming
    media_lib->streamings.clear();
#endif

    // close all file in media_lib->output_files
    for (auto &entry : media_lib->output_files)
    {
        entry.second.close();            
    }        

    std::cout << "Exiting." << std::endl;

    return 0;
}
