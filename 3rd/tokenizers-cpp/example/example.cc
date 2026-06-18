#include <tokenizers_cpp.h>

#include <fstream>
#include <iostream>
#include <string>

using tokenizers::Tokenizer;

std::string LoadBytesFromFile(const std::string& path) {
  std::ifstream fs(path, std::ios::in | std::ios::binary);
  if (fs.fail()) {
    std::cerr << "Cannot open " << path << std::endl;
    exit(1);
  }
  std::string data;
  fs.seekg(0, std::ios::end);
  size_t size = static_cast<size_t>(fs.tellg());
  fs.seekg(0, std::ios::beg);
  data.resize(size);
  fs.read(data.data(), size);
  return data;
}

void PrintEncodeResult(const std::vector<int>& ids) {
  std::cout << "tokens=[";
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i != 0) std::cout << ", ";
    std::cout << ids[i];
  }
  std::cout << "]" << std::endl;
}

// Sentencepiece tokenizer
// - dist/tokenizer.model
// void SentencePieceTokenizerExample() {
//   // Read blob from file.
//   auto blob = LoadBytesFromFile("dist/tokenizer.model");
//   // Note: all the current factory APIs takes in-memory blob as input.
//   // This gives some flexibility on how these blobs can be read.
//   auto tok = Tokenizer::FromBlobSentencePiece(blob);
//   std::string prompt = "What is the capital of Canada?";
//   // call Encode to turn prompt into token ids
//   std::vector<int> ids = tok->Encode(prompt);
//   // call Decode to turn ids into string
//   std::string decoded_prompt = tok->Decode(ids);

//   // print encoded result
//   std::cout << "SetencePiece tokenizer: " << std::endl;
//   PrintEncodeResult(ids);
//   std::cout << "decode=\"" << decoded_prompt << "\"" << std::endl;
// }

// HF tokenizer
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

  // print encoded result
  std::cout << "HF tokenizer: " << std::endl;
  PrintEncodeResult(ids);
  std::cout << "decode=\"" << decoded_prompt << "\"" << std::endl;
}

int main(int argc, char* argv[]) {
  // SentencePieceTokenizerExample();
  HuggingFaceTokenizerExample();
}
