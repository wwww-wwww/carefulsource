# CarefulSource

## Usage

```
cs.ImageSource(string path[, int subsampling_pad=True, int jpeg_rgb=True, int jpeg_fancy_upsampling=True])
```

- path: Path to image file
- subsampling_pad: Pad the image for subsampled images with odd resolutions
- jpeg_rgb: RGB output using internal JPEG upsampling for chroma
- jpeg_fancy_upsampling: libjpeg fancy chroma upscaling for rgb output

```
cs.ConvertColor(vnode clip, string output_profile[, string input_profile, int float_output=False])
```

- clip: Clip to process
- output_profile: Predefined profile ["xyz", "srgb", "srgb-gray"] or path to ICC profile to transform to
- input_profile: Profile to transform from
- float_output: Output as float

## Formats

- [ ] AVIF
- [ ] HEIC
- [ ] JPEG
  - [ ] libjpeg
    - [x] RGB
    - [x] YCbCr444
    - [x] YCbCr420
    - [x] YCbCr422
    - [x] YCbCr440
    - [x] L
    - [ ] CMYK
- [ ] JXL
- [x] PNG
  - [x] 8 bit RGBA
  - [x] 8 bit RGB
  - [x] 8 bit LA
  - [x] 8 bit L
  - [x] 16 bit RGBA
  - [x] 16 bit RGB
  - [x] 16 bit LA
  - [x] 16 bit L
- [ ] TIFF
- [ ] WEBP
