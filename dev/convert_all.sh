#!/bin/bash
set -e

for i in ttf/*
do
	echo "";
	echo "$i";
	python3 font_converter.py --add-numerals --outline-radius 32 "$i"
done
