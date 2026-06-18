# quick simple reference for eye ball
import tokenizers

def json_tokenizer():
    print("JSON tokenizer")
    tok = tokenizers.Tokenizer.from_pretrained("openai/clip-vit-large-patch14")
    text = "What is the capital of Canada?"
    ids = tok.encode(text).ids
    print(f"ids={ids}")
    decoded = tok.decode(ids, False)
    print(f"decoded={decoded}")

json_tokenizer()
