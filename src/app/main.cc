// main — main implementation.

#include "app/application.h"

int main(int argc, char* argv[]) {
    cosmo::app::Application app("Cosmo");

    app.run("..");

    return 0;
}