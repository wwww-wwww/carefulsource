# CarefulSource

## Usage

```
cs.ImageSource(string path)
```

```
cs.ConvertColor(vnode clip, string output_profile[, int float_output=0])
```

- output_profile: Predefined profile ["xyz", "srgb", "srgb-gray"] or path to ICC profile to transform to
- float_output: Output as float

## Formats

- [ ] AVIF
- [ ] HEIC
- [ ] JPEG
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
