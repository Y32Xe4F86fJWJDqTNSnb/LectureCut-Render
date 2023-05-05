<div align="center" style="display: grid; grid-template-columns: 1fr 100px 1fr; gap: 10px">
  <span></span>
  <img src=".github/static/Logo.svg" width=100/>
  <span style="font-family: Roboto Flex, Roboto; font-size: 1.5em; align-self: end; justify-self: left">Render</span>
</div>

---
<div align="center">
  <h3>If you like this project, please consider giving it a star ⭐️!</h3>
</div>

### ⚠️ THIS REPO IS STILL IN DEVELOPMENT ⚠️

## 📝 Description

The LectureCut Render is a module of the LectureCut project. It is responsible for rendering the final video from the cut list generated by a Generator.

## 🏗 Structure

Internally the Render module uses [`avcodec`](https://ffmpeg.org/libavcodec.html), [`avformat`](https://ffmpeg.org/libavformat.html), and similar from the [FFmpeg](https://ffmpeg.org/) project to handle cutting and transcoding. The Render module is written in [C++](https://isocpp.org/) to allow fast and efficient processing of videos.

<!-- license -->
## 📜 License

The LectureCut Render module is licensed under the [MIT License](LICENSE).