#we are using ubuntu base image
FROM ubuntu:latest

WORKDIR /mobius
RUN apt-get update && apt-get install -y \
    clang-14 \
    lldb-14 \
    lld-14 \
    libllvm-14-ocaml-dev \
    libllvm14 \
    llvm-14 \
    llvm-14-dev \
    llvm-14-runtime \
    && rm -rf /var/lib/apt/lists/*

CMD [ "/bin/bash" ]