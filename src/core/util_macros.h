#pragma once

#define GENERATE_LOCATOR(Class)                     \
export class Class##Locator {                       \
public:                                             \
    static Class * locate() {                       \
        return m_ClassInstance;                     \
    }                                               \
                                                    \
    static void provide(Class * instance) {         \
        m_ClassInstance = instance;                 \
    }                                               \
                                                    \
private:                                            \
    static Class * m_ClassInstance;                 \
};                                                  \
                                                    \
Class * Class##Locator::m_ClassInstance = nullptr;  \
