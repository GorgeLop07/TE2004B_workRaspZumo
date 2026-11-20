CXXFLAGS = -O2 -std=c++17 -Wall -Wno-format-security
all: gen bench
gen: gen_coeffs
	./gen_coeffs
gen_coeffs: gen_coeffs.cpp ../dsp/dsp_design.hpp ../dsp/dsp_config.h
	g++ $(CXXFLAGS) -o $@ gen_coeffs.cpp
bench: bench.cpp ../dsp/dsp_runtime.c ../dsp/dsp_runtime.h ../dsp/dsp_coeffs.h
	gcc -std=c11 -O2 -Wall -DDSP_COUNT_MAC -c ../dsp/dsp_runtime.c -o dsp_runtime.o
	g++ $(CXXFLAGS) -o bench bench.cpp dsp_runtime.o
run: gen bench
	./bench --noise noise --out out --trials 200 | tee out/bench_log.txt
	python3 plot.py
clean:
	rm -f gen_coeffs bench dsp_runtime.o
