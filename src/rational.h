
#ifndef MOBIUS_RATIONAL_H
#define MOBIUS_RATIONAL_H

#include <ostream>

template<typename T>
struct
Rational {
	T nom;
	T denom;
	
	Rational(T nom, T denom) : nom(nom) /* omnomnomnom */, denom(denom) { euclid_reduce();	}
	
	Rational(T nom) : Rational(nom, 1) {}
	
	Rational() : Rational(0) {}
	
	double to_double() { return (double)nom / (double)denom; }
	bool is_int() { return denom == 1; }
	void euclid_reduce();
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
operator/(const Rational<T> &a, int b) { return Rational<T>(a.nom, a.denom*b); }

template<typename T> inline Rational<T>&
operator*=(Rational<T> &a, int b) { return a = a*b; }

template<typename T> inline Rational<T>&
operator/=(Rational<T> &a, int b) { return a = a/b; }

template<typename T> inline bool
operator==(const Rational<T> &a, const Rational<T> &b) { return a.nom == b.nom && a.denom == b.denom; }

template<typename T> inline bool
operator!=(const Rational<T> &a, const Rational<T> &b) { return a.nom != b.nom || a.denom != b.denom; }

template<typename T> std::ostream &
operator<<(std::ostream &os, const Rational<T> &a) {
	os << a.nom;
	if(a.nom != 0 && a.denom != 1)
		os << "/" << a.denom;
	return os;
}

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

template<typename T> inline Rational<T>
pow_i(const Rational<T> &a, T b) {
	return pow_i(a.nom, b) / pow_i(a.denom, b);
}

template<typename T> inline void
Rational<T>::euclid_reduce() {
	if(denom < 0) {
		nom = -nom;
		denom = -denom;
	}
	T n = std::min((T)std::abs(nom), denom);
	T m = std::max((T)std::abs(nom), denom);
	while(n != 0) {
		T r = m % n;
		m = n;
		n = r;
	}
	nom   /= m;
	denom /= m;
}

#endif // MOBIUS_RATIONAL_H