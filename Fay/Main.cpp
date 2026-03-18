#include "App.h"

int main()
{
    using namespace fay;

    App::Desc desc;
    desc.WindowDesc.Api = API::Vulkan;

    App app(desc);
    app.Run();

    return 0;
}
