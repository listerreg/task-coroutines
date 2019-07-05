#ifndef AW_TASKCOROCONCEPTS_H
#define AW_TASKCOROCONCEPTS_H
namespace aw_coroutines {
#if __cpp_concepts >= 201507
template <class T>
concept bool NonReference = !std::is_reference<T>::value;

template <class T>
concept bool CopyConstructible = std::is_copy_constructible<T>::value;
#endif
}
#endif
