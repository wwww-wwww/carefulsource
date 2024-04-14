# CarefulSource

## Usage

```
cs.ImageSource(string path[, int subsampling_pad=True, int jpeg_rgb=True, int jpeg_fancy_upsampling=True])
```

- path: Path to image file
- subsampling_pad: Pad the image for subsampled images with odd resolutions
- jpeg_rgb: RGB output using internal JPEG upsampling for chroma
- jpeg_fancy_upsampling: libjpeg fancy chroma upscaling for rgb output
- jpeg_cmyk_profile: Path to force cmyk input profile
- jpeg_cmyk_target_profile: Path to force cmyk output profile - Predefined profiles ["srgb"]

```
cs.ConvertColor(vnode clip, string output_profile[, string input_profile, int float_output=False])
```

- clip: Clip to process
- output_profile: Path to ICC profile to transform to - Predefined profiles ["srgb", "srgb-gray", "xyz"]
- input_profile: Profile to transform from
- float_output: Output as float

## Formats

- [ ] AVIF
- [ ] HEIC
- [ ] JPEG
  - [x] libjpeg
    - [x] RGB
    - [x] YCbCr444
    - [x] YCbCr420
    - [x] YCbCr422
    - [x] YCbCr440
    - [x] L
    - [x] CMYK
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
