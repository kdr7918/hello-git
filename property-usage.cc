#include "property-store.h"
#include <iostream>

using namespace layout_property;

int main() {
    int shape = 0; // Multigon에서는 shape.base()를 사용합니다.
    PropertyStore store;

    // 1. Property 만들기: net_name = "VDD"
    Property property;
    property.name = "net_name";
    property.addText("VDD");

    // 2. Shape에 저장하기
    PropertyList properties;
    properties.push_back(property);
    store.saveShape(&shape, properties);
    store.finish(); // 모든 Shape를 저장한 후 한 번만 호출

    // 3. 같은 Shape 주소로 찾기
    PropertyList found = store.getShape(&shape);
    for (const Property& item : found) {
        for (const PropertyValue& value : item.values) {
            if (value.type == PropertyValue::Text)
                std::cout << item.name << " = " << value.text << '\n';
        }
    }
}
