# tokenizers-cpp

This project provides a cross-platform C++ tokenizer binding library that can be universally deployed.
It wraps and binds the [HuggingFace tokenizers library](https://github.com/huggingface/tokenizers)
and [sentencepiece](https://github.com/google/sentencepiece) and provides a minimum common interface in C++.

The main goal of the project is to enable tokenizer deployment for language model applications
to native platforms with minimum dependencies and remove some of the barriers of
cross-language bindings. This project is developed in part with and
used in [MLC LLM](https://github.com/mlc-ai/mlc-llm). We have tested the following platforms:

- iOS
- Android
- Windows
- Linux
- Web browser

## Getting Started

The easiest way is to add this project as a submodule and then
include it via `add_sub_directory` in your CMake project.
You also need to turn on `c++17` support.

- First, you need to make sure you have rust installed.
- If you are cross-compiling make sure you install the necessary target in rust.
  For example, run `rustup target add aarch64-apple-ios` to install iOS target.
- You can then link the library

See [example](example) folder for an example CMake project.

### Example Code

```c++
// - dist/tokenizer.json
void HuggingFaceTokenizerExample() {
  // Read blob from file.
  auto blob = LoadBytesFromFile("dist/tokenizer.json");
  // Note: all the current factory APIs takes in-memory blob as input.
  // This gives some flexibility on how these blobs can be read.
  auto tok = Tokenizer::FromBlobJSON(blob);
  std::string prompt = "What is the capital of Canada?";
  // call Encode to turn prompt into token ids
  std::vector<int> ids = tok->Encode(prompt);
  // call Decode to turn ids into string
  std::string decoded_prompt = tok->Decode(ids);
}

void SentencePieceTokenizerExample() {
  // Read blob from file.
  auto blob = LoadBytesFromFile("dist/tokenizer.model");
  // Note: all the current factory APIs takes in-memory blob as input.
  // This gives some flexibility on how these blobs can be read.
  auto tok = Tokenizer::FromBlobSentencePiece(blob);
  std::string prompt = "What is the capital of Canada?";
  // call Encode to turn prompt into token ids
  std::vector<int> ids = tok->Encode(prompt);
  // call Decode to turn ids into string
  std::string decoded_prompt = tok->Decode(ids);
}
```

### Extra Details

Currently, the project generates three static libraries
- `libtokenizers_c.a`: the c binding to tokenizers rust library
- `libsentencepice.a`: sentencepiece static library
- `libtokenizers_cpp.a`: the cpp binding implementation

If you are using an IDE, you can likely first use cmake to generate
these libraries and add them to your development environment.
If you are using cmake, `target_link_libraries(yourlib tokenizers_cpp)`
will automatically links in the other two libraries.
You can also checkout [MLC LLM](https://github.com/mlc-ai/mlc-llm)
for as an example of complete LLM chat application integrations.

## Javascript Support

We use emscripten to expose tokenizer-cpp to wasm and javascript.
Checkout [web](web) for more details.

## Acknowledgements

This project is only possible thanks to the shoulders open-source ecosystems that we stand on.
This project is based on sentencepiece and tokenizers library.
