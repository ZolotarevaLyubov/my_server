FROM ubuntu:22.04
RUN apt update && apt install -y g++ make
WORKDIR /app
COPY . .
RUN g++ -std=c++17 src/Server.cpp src/main.cpp -o server
CMD ["./server"]