#include "include/core/JContainer.h"
#include <iostream>

int main() {
    auto container0 = JContainer(0)._ref;
    auto container1 = JContainer(1)._ref;
    auto container2 = JContainer(2)._ref;
    container0->addChild(container1);
    container0->addChild(container2);

    for (size_t i = 0; i < container0->getChildren().size(); i++)
    {
        std::cout << container0->getChildren()[i]->getLayerId() << "parent " << container0->getChildren()[i]->getParent() << std::endl;
    }
    
};