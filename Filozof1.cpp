#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <ncurses.h>
#include <iostream>
#include <thread>
#include <fstream>
//macro ktore zamienia wszyskie LEFT i RIGHT na te formulki 
#define LEFT (ph_n + no_ph-1)%no_ph
#define RIGHT (ph_n + 1)%no_ph

//glowny lock na operacje
std::mutex lock;
//lock zeby mozna bylo bezpiecznie modyfikowac obiekt
std::mutex fork_lock;
//zmienna ktora moze wysylac sygnaly do innych watkow, a inne watki moga czekac na te sygnaly
std::condition_variable cv;
//bool ktory moze byc bezpiecznie shareowany pomiedzy threadami
std::atomic_bool run = true;
int stime = 2000;

//enum to zmienna ktora moze przyjmosc jeden z podanych stanow
enum State {
  THINKING,
  HUNGRY,
  EATING,
  STARVING,
};

//obiekt naszego widelca
struct Fork {
  //ktory filozof trzyma nasz widelec
  int picked_up_by = -1;
  //lock zeby mozna bylo bezpiecznie modyfikowac obiekt
  std::mutex cv_lock;
  //zmienna ktora moze wysylac sygnaly do innych watkow, a inne watki moga czekac na te sygnaly
  std::condition_variable cv;
  //funkcja ktora zwraca czy nasz widelec jest uzywany czy nie
  bool is_used() { if(picked_up_by == -1) { return false; } return true; }
};


class Philosopher {
  State s = THINKING;
  int id = 0;
  int number_of_hungry = 0;
  int number_of_starving = 0;
  public:
  //konstruktor
  Fork* left;
  Fork* right;
  Philosopher() {}
  void set_id(int id) { this->id = id; }
  int get_id() const { return id; }
  void set_state(State s) { this->s = s; }
  State get_state() { return s; }
  void im_hungry(){
    number_of_hungry++;
  }
  int get_im_hungry(){ return number_of_hungry;}
  void i_m_full(){number_of_hungry = 0;}
  void set_starving_counter(){ this->number_of_hungry = 0; number_of_starving++;}
  int get_number_of_starving(){ return number_of_starving;}
  void print() {
    int color = 1;
    const char* state;
    switch (s) {
      case HUNGRY: state = "HUNGRY  "; break;
      case THINKING: state = "THINKING"; break;
      case EATING: state = "EATING  "; break;
      case STARVING: state = "STARVING"; break;
    }
    //ustawiamy kursor na dana pozycja
    move(id+2, 0);
    printw("Filozof %d: ", id);
    printw("%s", state);
    printw(" |");
    refresh();
  }
};

class Waiter {
  //liczba filozofow
  int no_ph;
  //array filozofow
  Philosopher * phs;
  //array widelcow
  Fork * forks;

  public:
  //konstruktor kelnera
  Waiter(int N) {
    //ustawiamy liczbe filozofow na N
    no_ph = N;
    //inicjalizujemy nowa tablice filozofow
    phs = new Philosopher[N];
    //inicjalizujemy nowa tablice widelcow
    forks = new Fork[N];
    //ustawiamy kazdemu filozofowi jego id, oraz jego lewy i prawy widelec
    for(int i = 0; i < N; i++) {
      phs[i].set_id(i);
      phs[i].left = &forks[(i + no_ph-1)%no_ph];
      phs[i].right = &forks[(i + 1)%no_ph];
    }
  }
  //destruktor
  ~Waiter() {
    delete [] phs;
    delete [] forks;
  }
  //funckja odpowiedzialna za wyswietlanie informacji o dostepnosci poszczegolnego widelca
  void print_fork(bool is_used, int y, int x) {
    const char* s;
    if(is_used) { s = "|-|"; } else { s = "| |"; }
    move(y, x);
    printw("%s", s);
    refresh();
  }
  //funckja odpowiedzialna za wyswietlanie informacji o dostepnosci wszystkich widelcow
  void print_forks(int ph_n) {
    std::lock_guard<std::mutex> lg(fork_lock);
      bool used = false;
      if(phs[ph_n].left->picked_up_by == phs[ph_n].right->picked_up_by && phs[ph_n].left->picked_up_by != -1) { used = true; }
      print_fork(used, ph_n+2, 20);
      print_fork(used, ph_n+2, 23);
  }

  void check(int ph_n) {
    if(phs[LEFT].get_state() != EATING
        && phs[RIGHT].get_state() != EATING
        && phs[ph_n].get_state() == HUNGRY)
    {
      Fork * left = phs[ph_n].left;
      Fork * right = phs[ph_n].right;
      //zmienna ktora lockuje mutex az to konca scopa
      std::lock_guard<std::mutex> lgl(left->cv_lock);
      std::lock_guard<std::mutex> lgr(right->cv_lock);
      left->picked_up_by = ph_n;
      right->picked_up_by = ph_n;
      phs[ph_n].set_state(EATING);
      phs[ph_n].print();
      print_forks(ph_n);
    }
  }

  void take_fork(int ph_n) {
    //tutaj jest unique lock zamiast lock guard poniewaz condition variable potrzebuje unique locka
    std::unique_lock<std::mutex> u_lock(lock);
    phs[ph_n].set_state(HUNGRY);
    check(ph_n);
    if(phs[ph_n].get_state() != EATING) {
      phs[ph_n].im_hungry();  
      forks[ph_n].cv.wait(u_lock);
    }
    phs[ph_n].print();
    print_forks(ph_n);
  }

  void put_fork(int ph_n) {
    if(phs[ph_n].get_state() == HUNGRY) {return;}
    std::lock_guard<std::mutex> lg(lock);
    phs[ph_n].set_state(THINKING);
    {
      Fork * left = phs[ph_n].left;
      std::lock_guard<std::mutex> lg(left->cv_lock);
      left->picked_up_by = -1;
      left->cv.notify_one();
    }
    {
      Fork * right = phs[ph_n].right;
      std::lock_guard<std::mutex> lg(right->cv_lock);
      right->picked_up_by = -1;
      right->cv.notify_one();
    }

    check(RIGHT);
    check(LEFT);
    phs[ph_n].print();
    print_forks(ph_n);
  }
};

void sleep(int millis) {
  std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

void philosopher_routine(Waiter &w, int i) {
  while(run) {
    sleep(rand()%400 + 6000);
    w.take_fork(i);
    sleep(6000);
    w.put_fork(i);
  }
}

int main(int argc, char ** argv) {

  if(argc < 2) { 
    std::cerr << "Error: No parameter provided" << std::endl; 
    return 1; 
  }
  const int N = std::stoi(argv[1]);
  if (N < 5 || N > 10) {
    std::cerr << "Error: Number out of range, should be between 5 and 10.\nor The number must come first" << std::endl;
    return 1;
  }
  bool logs = false;
  if(argc > 3){
    std::string log_c = argv[2];
    if(log_c == "true" || log_c == "tak"){
      logs = true;
    }
  }

  initscr();
  //inicjalizujemy naszego kelnera
  Waiter waiter(N);
  //array watkow
  std::thread threads[N];
  //robimy nowe watki
  for(int i = 0; i < N; i++) {
    threads[i] = std::thread(philosopher_routine, std::ref(waiter), i);
  }
  move(0,0);
  printw("===========================");
  move(1, /* y */0);
  printw("                     U||D  ");
  move(N+3, 0);
  printw("===========================");
  //blockuje glowny thread az do inputu uzytkownika
  getch();
  //uzytkownik kliknal jakikolwike przycisk, wychodzimy
  run = false;
  clear();
  printw("waiting for threads to end");
  refresh();
  //czekamy az thready skoncza swoje dzialanie
  for(int i = 0; i < N; i++) {
    threads[i].join();
  }
  endwin();
  
  return 0;
}