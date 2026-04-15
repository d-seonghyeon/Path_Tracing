#include "image.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

ImageUPtr Image::Load(const std::string& filepath){

    auto image = ImageUPtr(new Image());
    if (!image ->LoadWithStb(filepath))
        return nullptr;
    return std::move(image);
}

ImageUPtr Image::Create(int width, int height, int channelCount){

    auto image = ImageUPtr(new Image());
    if(!image->Allocate(width,height,channelCount))
        return nullptr;
    return std::move(image);


}

bool Image::Allocate(int width, int height, int channelCount) {
    m_width = width;
    m_height = height;
    m_channelCount = channelCount;
    m_data = (uint8_t*)stbi__malloc(m_width * m_height * m_channelCount); 
    return m_data != nullptr;
}



Image:: ~Image(){

    if(m_data){
        stbi_image_free(m_data);
    }
}

bool Image::LoadWithStb(const std::string & filepath){
    m_data = stbi_load(filepath.c_str(), &m_width, &m_height, &m_channelCount, 4);
    
    if(!m_data){
        SPDLOG_ERROR("failed to load image: {}",filepath);
        return false;
    }
    m_channelCount=4;
    return true;

}

