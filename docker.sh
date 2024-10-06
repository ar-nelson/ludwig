#!/bin/sh
docker build ./docker -t ludwig-builder:latest
docker run -it --rm -v $(pwd):/home/ubuntu/ludwig -p 2023:2023 ludwig-builder:latest
