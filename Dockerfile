FROM intel/oneapi:2025.2.0-0-devel-ubuntu22.04

RUN apt update && apt upgrade -y && \
  ln -sf /usr/share/zoneinfo/Australia/Sydney /etc/localtime && \
  echo "Australia/Sydney" > /etc/timezone && \
  apt install -y sudo vim build-essential git cmake libgc-dev libpugixml-dev \
  clang-12 llvm-12 apt-transport-https dotnet-sdk-8.0 software-properties-common \
  python3 python3-pip default-jdk pypy3 mono-runtime nodejs autoconf && \
  sudo add-apt-repository -y ppa:plt/racket && \
  apt update && apt install -y racket && \
  raco pkg install --auto grift && \
  wget https://packages.microsoft.com/config/ubuntu/22.04/packages-microsoft-prod.deb \
  -O packages-microsoft-prod.deb && \
  sudo dpkg -i packages-microsoft-prod.deb && \
  rm packages-microsoft-prod.deb && \
  pip3 install -U plotly && \
  pip3 install -U pandas && \
  pip3 install -U kaleido

WORKDIR /home/monnom
