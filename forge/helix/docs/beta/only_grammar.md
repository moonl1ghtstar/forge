Helix 문법

이 문서는 시각적 효율성을 고려하지 않고 Helix에서 사용 가능한 모든 구문(또는 앞으로 만들 구문)들을 나열한 문서이다.

1. import 구문:
 모두 불러오기(forge 컴파일러 루트): import <moudule>
 모두 불러오기(프로젝트 루트): import "<file>.hlx"
 일부 함수만 불러오기(forge 루트): import <module> { <func>, <func> }
 일부 함수만 불러오기(프로젝트 루트): import "<file>.hlx" { <func>, <func> }

 함수 쓸때는 <module>.<func>(); 이렇게 사용

2. 데이터 타입:
 int 정수
 str 문자, 문자열
 bool 불리언
 float 소숫점
 long 매우 큰 숫자
 global <자료형> 파일 전역 + import해도 가져감
 const 상수
 var 파일 전역 동적 타입
 let 블록 전역 동적 타입

 변수, 상수명은 Camel

3. if 문:
 ```
 if(True) {
    // codes
 } else if(True) {
    // codes
 } else {
    // codes
 }
 ```

4. switch 문:
 switch {
    case(True) {
        // codes
    }
    case(False) {
        // codes
    }
    case {
        //괄호 없고 괄호 있는것 중에서 True가 없으면 자동으로 이 케이스 실행
    }
 }

5. for 문:
 for(int i = 0; i = 5; i++) {
    // codes
 }
 변수 선언, i = 5면 break, 증강식

6. while 문:
 while(True) {
    // codes
 }
 True 일때 실행
 flow: True 검증, 실행

7. until 문:
 until(False) {
    // codes
 }
 False 일때 실행
 flow: False 검증, 실행

8. do 문:
 do {
    // codes
 } while(True)
 flow: 실행, 검증

9. break 문:
 while(True) {
    if(a == 5) {
        break();
    }
 }
 반복 중지

10. pass 문:
 for(int i = 0; i = 5; i++) {
    if(i == 3) {
        pass();
    }
 }
 현재 반복 건너뛰기

11. 함수 선언문:
 function <함수명>(arguments) {
    // codes
 }

 call:
  <함수명>();
 return:
  function <func>(args) {
    return;
  }
  이렇게 return 뒤에 아무것도 없으면 자동으로 void
 함수명은 Camel