// MyVideoAndAudioPlayer.cpp: 定义应用程序的入口点。
//

#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>

using namespace std;
std::mutex accountMutex;
int account = 10;
void testThread1() {
	cout << "in thread1" << endl;
}
void testThread2(int a) {
	cout << "in thread2 " << a << endl;
}
void addAccount(int a) {
	std::lock_guard<std::mutex> lk(accountMutex);
	account += a;
	cout << account << endl;
}
void cutAccount(int num) {
	std::lock_guard<std::mutex> lk(accountMutex);
	account -= num;
	cout << account << endl;
}
/*int main()
{
	cout << "Hello CMake." << endl;
	int a = 3;
	std::thread t1{testThread1};
	t1.detach();
	std::thread t2{testThread2,a};
	t2.detach();
	

	for (int i = 0;i < 1000;i++) {
		std::thread t3(addAccount, 1);
		std::thread t4(cutAccount, 1);
		t3.detach();
		t4.detach();
	}
	std::this_thread::sleep_for(std::chrono::seconds(2));
	cout << account << endl;
	return 0;
}*/
