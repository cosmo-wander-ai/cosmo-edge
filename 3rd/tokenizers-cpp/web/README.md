# WebTokenizers

| [NPM Package](https://www.npmjs.com/package/@mlc-ai/web-tokenizers) | [WebLLM](https://github.com/mlc-ai/web-llm) |

WebTokenizers is a javascript binding library that can be universally deployed.
It wraps and binds the [HuggingFace tokenizers library](https://github.com/huggingface/tokenizers)
and [sentencepiece](https://github.com/google/sentencepiece) and provides a minimum common interface
in javascript.

## Build from Source

```bash
source /path/to/emsdk_env.sh
npm install
npm run build
```

To try out the test webpage
```bash
cd tests
npm start
```
