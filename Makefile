export PATH=/home/chenxy/android-ndk/android-ndk-r27/toolchains/llvm/prebuilt/linux-x86_64/bin/:$PATH
romw:romwriter.cpp
	aarch64-linux-android33-clang -MD -MF -shared -g -fPIE romwriter.cpp -o romw