#include "App.h"

int main()
{
    using namespace fay;

    App::Desc desc;
    App app(desc);
    app.Run();

    return 0;
}
