# What is this

I will graduate from university and start working from next month on. I want to learn more about C++ so that I can do my work better in the future.
I started this project For leaning boost::asio.

# How to compile and run

You should install boost on your computer and ensure your comliler can access head files and link libraries of boost::asio.

For me, I simply used `apt install libboost-all-dev` under ununtu and it worked properly.

After installed boost, I use `g++ server.cpp -o server -lpthread -lboost_system` to compile `server.cpp`, and the same as `client.cpp`.

After the exctutable files `server` and `client` has been built, you should run `server` at first, then `client`. The usage will been shown on screen so don't worry about how to use them.

There're may bugs which I have not fixed. And I don't plan to fix them since I created this project just for practicing boost::asio but not for commercial use.
