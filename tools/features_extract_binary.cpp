//#include <cuda_runtime.h>

#include <cfloat>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc/imgproc.hpp>

#include "boost/algorithm/string.hpp"
#include "google/protobuf/text_format.h"

#include "caffe/caffe.hpp"
//#include "caffe/blob.hpp"

using namespace std;
using namespace caffe;  // NOLINT(build/namespaces)

int main(int argc, char** argv) {
  ::google::InitGoogleLogging(argv[0]);
	if (argc < 5) {
    LOG(ERROR) << std::endl
      << "features_extract_binary_trimean" << std::endl
      << "    net-deploy.proto.txt" << std::endl
      << "    pretrained-net-model.proto" << std::endl
      << "    data-mean.proto" << std::endl
      << "    CPU/GPU device-id" << std::endl
      << "    frame-dir file-list" << std::endl
      << "    output-file-base-name layers(separated by commas)";
    return 0;
  }

  int device_id = 0;
  if (strcmp(argv[4], "GPU") == 0) {
    device_id = atoi(argv[5]);
    LOG(ERROR) << "Using GPU " << device_id;
    Caffe::SetDevice(device_id);
    Caffe::set_mode(Caffe::GPU);
  } else if (strcmp(argv[4], "CPU") == 0) {
    LOG(ERROR) << "Using CPU";
    Caffe::set_mode(Caffe::CPU);
  }

  // Read model paramters
	boost::shared_ptr<Net<float> > caffe_test_net(
			new Net<float>(argv[1], caffe::TEST) );
	caffe_test_net->CopyTrainedLayersFrom(argv[2]);

  
  // Read mean file
  float* mean = NULL;
  float mean_acquired[3] = {104, 117, 123};
  if (strcmp(argv[3], "104,117,123") != 0) {
    LOG(INFO) << "Loading mean file from" << argv[3];
    BlobProto blob_proto;
    ReadProtoFromBinaryFileOrDie(argv[3], &blob_proto);
    Blob<float> data_mean_;
    data_mean_.FromProto(blob_proto);
    const float* mean_tmp = data_mean_.cpu_data();
    mean = new float[3 * 256 * 256];
    for (int i=0; i < 3 * 256 * 256; i++) mean[i] = mean_tmp[i];
    LOG(INFO) << "Load mean done!";
  }

  // Read file list
  string frame_dir = argv[6];
  std::ifstream infile(argv[7]);
  std::vector<string> frame_names;
  std::vector<string> frame_files;
  string frame_name;
  while(getline(infile, frame_name)) {
    frame_names.push_back(frame_name);
    frame_files.push_back(frame_name);
  }

  // Read layers
  string output_file_base = argv[8];
  string original_layers = argv[9];
  vector<string> layers;
  boost::split(layers, original_layers, boost::is_any_of(","));
  int n_layer = layers.size();
  FILE *output_files[n_layer];
  for (int i = 0; i < n_layer; i++) {
    string output_layer = layers[i];
    for (int j = 0; j < output_layer.size(); j++) {
			if (output_layer[j] == '/') output_layer[j] = '_';
		}
		string output_file_name = output_file_base + ".bin." + output_layer;
    LOG(INFO) << "writing layer " << layers[i] << " into " << output_file_name << std::endl;
    output_files[i] = fopen(output_file_name.c_str(), "wb");
    const int frame_number = frame_files.size();
    const int feat_dim = caffe_test_net->blob_by_name(layers[i])->channels();
    fwrite(&frame_number, sizeof(int), 1, output_files[i]);
    fwrite(&feat_dim, sizeof(int), 1, output_files[i]);
  }

  cv::Mat cv_img, cv_img_orig;
  int image_size = 256;
  if (argc > 10) image_size = atoi(argv[10]);

  int batch_size = caffe_test_net->blob_by_name("data")->num();
  int n_channel = caffe_test_net->blob_by_name("data")->channels();
	int image_crop_size = caffe_test_net->blob_by_name("data")->height();
	int n_image_crop_pixel = n_channel*image_crop_size*image_crop_size;
	
	int height_offset = (image_size - image_crop_size) / 2;
	int width_offset = (image_size - image_crop_size) / 2;

	LOG(INFO) << caffe_test_net->name();
  //LOG(INFO) << caffe_test_net->num_inputs() << ' ' << caffe_test_net->num_outputs(); 
	LOG(INFO) << caffe_test_net->blob_by_name("data")->num();
	LOG(INFO) << caffe_test_net->blob_by_name("data")->channels();
  LOG(INFO) << caffe_test_net->blob_by_name("data")->height();
  LOG(INFO) << caffe_test_net->blob_by_name("data")->width();

	std::vector<string> batch_frame_names;
  std::vector<Blob<float>*> batch_frame_blobs;
  Blob<float>* frame_blob = new Blob<float>(batch_size, n_channel, image_crop_size, image_crop_size);
  for (int frame_id = 0; frame_id < frame_files.size(); ++frame_id) {
    batch_frame_names.push_back(frame_names[frame_id]);

    cv_img_orig = cv::imread(frame_dir + "/" + frame_files[frame_id], CV_LOAD_IMAGE_COLOR);
    cv::resize(cv_img_orig, cv_img, cv::Size(image_size, image_size));

    float* frame_blob_data = frame_blob->mutable_cpu_data();
    for (int i_channel = 0; i_channel < n_channel; ++i_channel) {
      for (int height = 0; height < image_crop_size; ++height) {
        for (int width = 0; width < image_crop_size; ++width) {
          int insertion_index = (frame_id % batch_size)* n_image_crop_pixel + (i_channel * image_crop_size + height) * image_crop_size + width;
          int mean_index = (i_channel * image_size + height + height_offset)*image_size + width + width_offset;
          float mean_value = mean_acquired[i_channel];
          if (mean != NULL) mean_value = mean[mean_index];
          frame_blob_data[insertion_index] = static_cast<float>(cv_img.at<cv::Vec3b>(height + height_offset, width + width_offset)[i_channel]) - mean_value;
        }
      }
    }

    if ((frame_id + 1) % batch_size == 0 || (frame_id + 1) == frame_files.size()) {
      batch_frame_blobs.push_back(frame_blob);
      //const vector<Blob<float>*>& result = caffe_test_net->Forward(batch_frame_blobs);
      caffe_test_net->Forward(batch_frame_blobs);

      const int n_example = batch_frame_names.size();
      for (int i_layer = 0; i_layer < n_layer; ++i_layer) {
        const shared_ptr<Blob<float> > data_blob = caffe_test_net->blob_by_name(layers[i_layer]);
        const float* data_blob_ptr = data_blob->cpu_data();
        const int n_channel = data_blob->channels();
				fwrite(data_blob_ptr, sizeof(float), n_example * n_channel, output_files[i_layer]);
				/*
				int* data_blob_int = new int[n_channel];
				for(int i=0; i < n_example; i++){ // the number of images
					//write the feature of a image
					for (int j=0; j < n_channel; j++) {
						data_blob_int[j] = (int)(data_blob_ptr[i * n_channel + j] * 10000);
					}
					fwrite(data_blob_int, sizeof(int), n_channel, output_files[i_layer]);
				}//fwrite
				*/
			}

      LOG(INFO) << frame_id + 1 << " (+" << n_example
        << ") out of " << frame_files.size() << " results written.";

      batch_frame_names.clear();
      batch_frame_blobs.clear();
    }
  }


  for (int i = 0; i < n_layer; i++) {
    fclose(output_files[i]);
  }

  delete frame_blob;

  return 0;
}
