
#ifndef MOBIUS_RATIONAL_H
#define MOBIUS_RATIONAL_H

#include <ostream>

template<typename T>
struct
Rational {
	T nom;
	T denom;
	
	Rational(T nom, T denom) : nom(nom) /* omnomnomnom */, denom(denom) {
		//TODO: reduce
	}
	
	Rational(T nom) : Rational(nom, 1) {}
	
	Rational() : Rational(0) {}
	
	operator double() { return (double)nom / (double)denom; }
	bool is_int() { return denom == 1; }
	
	std::ostream &operator<<(std::ostream &os);
};

template<typename T> inline Rational<T>
operator*(const Rational<T> &a, const Rational<T> &b) {	return Rational<T>(a.nom*b.nom, a.denom*b.denom); }

template<typename T> inline Rational<T>
operator/(const Rational<T> &a, const Rational<T> &b) {	return Rational<T>(a.nom*b.denom, a.denom*b.nom); }

template<typename T> inline Rational<T>
operator+(const Rational<T> &a, const Rational<T> &b) {	return Rational<T>(a.nom*b.denom + b.nom*a.denom, a.denom*b.denom); }

template<typename T> inline Rational<T>
operator-(const Rational<T> &a) { return Rational<T>(-a.nom, a.denom); }

template<typename T> inline Rational<T>
operator-(const Rational<T> &a, const Rational<T> &b) {	return a + (-b); }

template<typename T> inline Rational<T> &
operator+=(Rational<T> &a, const Rational<T> &b) { return a = a + b; }

template<typename T> inline Rational<T> &
operator-=(Rational<T> &a, const Rational<T> &b) { return a = a - b; }

template<typename T> inline Rational<T> &
operator*=(Rational<T> &a, const Rational<T> &b) { return a = a * b; }

template<typename T> inline Rational<T> &
operator/=(Rational<T> &a, const Rational<T> &b) { return a = a / b; }

template<typename T> inline Rational<T>
operator*(const Rational<T> &a, int b) { return Rational<T>(a.nom*b, a.denom); }

template<typename T> inline Rational<T>
operator*(int b, const Rational<T> &a) { return Rational<T>(a.nom*b, a.denom); }

template<typename T> inline Rational<T>
pow_i(T a, T b) {
	//TODO: inefficient;
	Rational<T> result = {1, 1};
	if(b > 0)
		for(T p = 0; p < b; ++p) result.nom *= a;
	else if(b < 0)
		for(T p = 0; p < -b; ++p) result.denom *= a;
	return result;
}

#endif // MOBIUS_RATIONAL_H