//---------------------------------------------------
//
//         Ruby Enhancing library
//
//              Speedpetal
//
//  Thumbnail image is made at high speed. 
//
//        2009/03/04  Tsukasa OISHI
//
//
//
//  Return Speedpetal version
//  Speedpetal::version
//
//  Make thumbnail image
//  Speedpetal::resize(size, origin_file, output_file)
//
//  Make square thumbnail image
//  Speedpetal::resize_square(size, origin_file, output_file)
//
//
//---------------------------------------------------
#include <stdio.h>
#include <ruby.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <jpeglib.h>

typedef  struct image_size {
    int width;
    int height;
} IMAGESIZE;

// The ratio of resizes is requested.
double get_scale_factor(int target_size, int width, int height, int square) {
    int denominator = square ? (width > height ? height : width) : (width > height ? width : height);
    return (double)target_size / denominator;
}

// The size after making thumbnail image.
IMAGESIZE get_after_size(int request_size, struct jpeg_decompress_struct *in_info, int square) {
    IMAGESIZE i_size;
    double scale_factor = get_scale_factor(request_size, in_info->output_width, in_info->output_height, square);
    i_size.width = i_size.height = request_size;

    if((square && in_info->output_width > in_info->output_height) || (!square && in_info->output_width < in_info->output_height)) {
        i_size.width = (int)(in_info->output_width * scale_factor);
    } else {
        i_size.height = (int)(in_info->output_height * scale_factor);
    }
    return i_size;
}

// configs.
void speed_setting(int request_size, struct jpeg_decompress_struct *in_info, int square) {
    jpeg_calc_output_dimensions(in_info);
    in_info->scale_denom = (unsigned int)(1 / get_scale_factor(request_size, in_info->output_width, in_info->output_height, square));
    in_info->two_pass_quantize = FALSE;
    in_info->dither_mode = JDITHER_ORDERED;
    if (!in_info->quantize_colors) { 
        in_info->desired_number_of_colors = 216;
    }
    in_info->dct_method = JDCT_FASTEST;
    in_info->do_fancy_upsampling = FALSE;
}

// memory allocate.
JSAMPARRAY sppedpetal_alloc(int width, int height, int components) {
    int i;
    JSAMPARRAY img = (JSAMPARRAY)malloc(sizeof(JSAMPROW) * height);
    for(i = 0; i < height; i++){
        img[i] = (JSAMPROW)calloc(sizeof(JSAMPLE), width * components);
    }
    return img;
}

// memory free.
void speedpetal_free(JSAMPARRAY buffer, int height) {
    int i;
    for(i = 0; i < height; i++){
        free(buffer[i]);
    }
    free(buffer);
}

// create thumbnail image.
void create_thumbnail(JSAMPARRAY in_buffer, JSAMPARRAY out_buffer, int origin_width, int origin_height, int width, int height, int target_size, int components) {
    double width_factor = (double)origin_width / width;
    double height_factor = (double)origin_height / height;
    int width_pos[width];
    int x;
    int y;
    int start_x = 0;
    int start_y = 0;

    if(target_size) {
        width > height ? (start_x = (width - target_size) / 2) : (start_y = (height - target_size) / 2);
        width = height = target_size;
    }

    for(x = 0; x < width; x++) {
        width_pos[x] = (int)((x + start_x) * width_factor) * components;
    }

    for(y = 0; y < height; y++) {
        int height_pos = (int)((y + start_y) * height_factor);
        for(x = 0; x < width; x++) {
            memcpy(&out_buffer[y][x * components], &in_buffer[height_pos][width_pos[x]], components);
        }
    }
}

// main process
void abstract_resize(VALUE request_size, VALUE in_file_name, VALUE out_file_name, int square){
    Check_Type(in_file_name, T_STRING);
    Check_Type(out_file_name, T_STRING);
    int target_size = NUM2INT(request_size);

    FILE *infile;
    if((infile = fopen(StringValuePtr(in_file_name), "rb")) == NULL){
        rb_raise(rb_eRuntimeError, "Can't open in_file");
    }

    FILE *outfile;
    if((outfile = fopen(StringValuePtr(out_file_name), "wb")) == NULL){
        fclose(infile);
        rb_raise(rb_eRuntimeError, "Can't open out_file");
    }

    // decompress
    struct jpeg_decompress_struct in_info;
    struct jpeg_error_mgr jpeg_error;
    in_info.err = jpeg_std_error(&jpeg_error);

    jpeg_create_decompress(&in_info);
    jpeg_stdio_src(&in_info, infile);
    jpeg_read_header(&in_info, TRUE);
    
    speed_setting(target_size, &in_info, square);
    
    int components = in_info.output_components;
    J_COLOR_SPACE color_space = components == 3 ? JCS_RGB : JCS_GRAYSCALE;
    IMAGESIZE image_size = get_after_size(target_size, &in_info, square);

    jpeg_start_decompress(&in_info);

    JSAMPARRAY buffer = (JSAMPARRAY)sppedpetal_alloc(in_info.output_width, in_info.output_height, components);

    while(in_info.output_scanline < in_info.output_height){
        jpeg_read_scanlines(&in_info, buffer + in_info.output_scanline, in_info.output_height - in_info.output_scanline);
    }

    JSAMPARRAY img = (JSAMPARRAY)sppedpetal_alloc(image_size.width, image_size.height, components);
    create_thumbnail(buffer, img, in_info.output_width, in_info.output_height, image_size.width, image_size.height, square ? target_size : 0, components);

    speedpetal_free(buffer, in_info.output_height);
    jpeg_finish_decompress(&in_info);
    jpeg_destroy_decompress(&in_info);
    fclose(infile);

    // compress
    struct jpeg_compress_struct out_info;
    out_info.err = jpeg_std_error(&jpeg_error);
    jpeg_create_compress(&out_info);
    jpeg_stdio_dest(&out_info, outfile);

    if (square) {
        out_info.image_width = target_size;
        out_info.image_height = target_size;
    } else {
        out_info.image_width = image_size.width;
        out_info.image_height = image_size.height;
    }

    out_info.input_components = components;
    out_info.in_color_space = color_space;
    jpeg_set_defaults(&out_info);

    jpeg_start_compress(&out_info, TRUE);
    jpeg_write_scanlines(&out_info, img, out_info.image_height);
    jpeg_finish_compress(&out_info);
    jpeg_destroy_compress(&out_info);
    fclose(outfile);

    speedpetal_free(img, image_size.height);
}

/* Thumbnail image create */
static VALUE t_resize(VALUE self, VALUE request_size, VALUE in_file_name, VALUE out_file_name){
    abstract_resize(request_size, in_file_name, out_file_name, 0);
    return self;
}

/* Square thumbnail image create */
static VALUE t_resize_square(VALUE self, VALUE request_size, VALUE in_file_name, VALUE out_file_name){
    abstract_resize(request_size, in_file_name, out_file_name, 1);
    return self;
}

// initial
void Init_speedpetal() {
    VALUE cSpeedpetal;

    cSpeedpetal = rb_define_module("Speedpetal");

    /* Thumbnail image create */
    //	request_size: thumbnail size
    //	in_file_name: origin image path
    //	out_file_name: thumbnail image path
    rb_define_module_function(cSpeedpetal, "resize", t_resize, 3);

    /* Square thumbnail image create */
    //	request_size: thumbnail size
    //	in_file_name: origin image path
    //	out_file_name: thumbnail image path
    rb_define_module_function(cSpeedpetal, "resize_square", t_resize_square, 3);
}
