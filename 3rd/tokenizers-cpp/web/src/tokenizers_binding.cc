#include <emscripten.h>
#include <emscripten/bind.h>
#include <tokenizers_cpp.h>

#include <memory>

emscripten::val vecIntToView(const std::vector<int>& vec) {
  return emscripten::val(emscripten::typed_memory_view(vec.size(), vec.data()));
}

EMSCRIPTEN_BINDINGS(tokenizers) {
  emscripten::register_vector<int>("VectorInt");
  emscripten::function("vecIntToView", &vecIntToView);
  emscripten::function("vecIntFromJSArray",
                       emscripten::select_overload<std::vector<int>(const emscripten::val&)>(
                           &emscripten::vecFromJSArray));

  emscripten::class_<tokenizers::Tokenizer>("Tokenizer")
      .smart_ptr<std::shared_ptr<tokenizers::Tokenizer>>("Tokenizer")
      .class_function("FromBlobJSON", &tokenizers::Tokenizer::FromBlobJSON)
      .class_function("FromBlobByteLevelBPE", &tokenizers::Tokenizer::FromBlobByteLevelBPE)
      .class_function("FromBlobSentencePiece", &tokenizers::Tokenizer::FromBlobSentencePiece)
      .function("Encode", &tokenizers::Tokenizer::Encode)
      .function("Decode", &tokenizers::Tokenizer::Decode);
}
