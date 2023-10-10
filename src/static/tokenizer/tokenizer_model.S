    .global tokenizer_model
    .global tokenizer_model_size
    .section .rodata
tokenizer_model:
    .incbin "static/tokenizer/en.wiki.bpe.vs200000.model"
1:
tokenizer_model_size:
    .int 1b - tokenizer_model
